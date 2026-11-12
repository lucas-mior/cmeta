// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_MEMBER_ORDER_C)
#define CSANECHECK_MEMBER_ORDER_C

#include "cbase.h"
#include "csck.h"
#include "csck_tokens.h"

typedef struct CsaneArrayOrderMember {
    int32 name_token;
    int32 branch;
    bool pointer;
} CsaneArrayOrderMember;

static int32
csane_order_next(CsaneFile *file, TokenRange range, int32 previous) {
    int32 i = token_range_next_significant(file->tokens, range, previous);
    while (i < range.end) {
        int32 region = csane_regions_at_token(file->regions, i);
        if (region >= 0
            && file->regions->items[region].kind == CSANE_REGION_CODE) {
            break;
        }
        i = token_range_next_significant(file->tokens, range, i);
    }
    return i;
}

static bool
csane_order_compatible(CsaneFile *file, CsaneArrayOrderMember *a,
                       CsaneArrayOrderMember *b) {
    CsaneRegions *regions = file->regions;
    for (int32 first = a->branch; first >= 0;
         first = regions->items[first].parent) {
        CsaneSourceRegion *left = &regions->items[first];
        if (left->kind != CSANE_REGION_CONDITIONAL_BRANCH) {
            continue;
        }
        for (int32 second = b->branch; second >= 0;
             second = regions->items[second].parent) {
            CsaneSourceRegion *right = &regions->items[second];
            if (right->kind == CSANE_REGION_CONDITIONAL_BRANCH
                && left->parent == right->parent && first != second) {
                return false;
            }
        }
    }
    return true;
}

static bool
csane_order_matching(Token *pointer, Token *length) {
    if (length->len == pointer->len + 1 && length->text[0] == 'n'
        && !memcmp64(pointer->text, length->text + 1, pointer->len)) {
        return true;
    }
    return length->len == pointer->len + 4
           && !memcmp64(pointer->text, length->text, pointer->len)
           && !memcmp64(length->text + pointer->len, "_len", 4);
}

static void
csane_order_struct(CsaneFile *file, int32 open, int32 close) {
    Tokenization *tokens = file->tokens;
    CsaneTokenView view;
    CsaneArrayOrderMember *members = NULL;
    if (!csane_tokens_view(&view, tokens, (TokenRange){open + 1, close})) {
        return;
    }
    for (int32 start = open + 1; start < close;) {
        int32 end = csane_tokens_top_level(&view, start, STRLIT(";"));
        CsaneArrayOrderMember member = {.name_token = -1};
        if (end < 0 || end >= close) {
            break;
        }
        TokenRange range = {start, end};
        int32 first = csane_order_next(file, range, range.first - 1);
        int32 name = -1;
        int32 stars = 0;
        int32 first_type = -1;
        int32 type_next = -1;
        bool valid = true;
        for (int32 k = first; k < range.end;
             k = csane_order_next(file, range, k)) {
            Token *token = &tokens->tokens[k];
            if (TOKEN_IS(token, ",") || TOKEN_IS(token, "(")
                || TOKEN_IS(token, ")") || TOKEN_IS(token, "[")
                || TOKEN_IS(token, "]") || TOKEN_IS(token, "{")
                || TOKEN_IS(token, "}") || TOKEN_IS(token, ":")
                || TOKEN_IS(token, "=")) {
                valid = false;
                break;
            }
            if (TOKEN_IS(token, "*")) {
                stars += 1;
            }
            name = k;
            if (first_type < 0 && !TOKEN_IS(token, "const")
                && !TOKEN_IS(token, "volatile")
                && !TOKEN_IS(token, "restrict")
                && !TOKEN_IS(token, "_Atomic")) {
                first_type = k;
            } else if (first_type >= 0 && type_next < 0) {
                type_next = k;
            }
        }
        if (name < 0 || first_type < 0
            || tokens->tokens[name].kind != TOKEN_IDENT) {
            valid = false;
        }
        if (valid && stars == 0) {
            Token *type = &tokens->tokens[first_type];
            if ((!TOKEN_IS(type, "int16") && !TOKEN_IS(type, "int32")
                 && !TOKEN_IS(type, "int64")) || type_next != name) {
                valid = false;
            }
        } else if (valid && (first_type == name || type_next < 0)) {
            valid = false;
        }
        if (valid) {
            int32 region = csane_regions_at_token(file->regions, name);
            int32 branch =
                csane_regions_enclosing(file->regions, region,
                                        CSANE_REGION_CONDITIONAL_BRANCH);
            member = (CsaneArrayOrderMember){
                .name_token = name,
                .branch = branch,
                .pointer = stars > 0,
            };
        }
        ARRAY_PUSH(members, member);
        start = end + 1;
    }
    for (int32 i = 0; i + 3 < ARRAY_LEN(members); i += 1) {
        CsaneArrayOrderMember *first = &members[i];
        CsaneArrayOrderMember *first_len = &members[i + 1];
        CsaneArrayOrderMember *second = &members[i + 2];
        CsaneArrayOrderMember *second_len = &members[i + 3];
        bool compatible = true;
        if (first->name_token < 0 || first_len->name_token < 0
            || second->name_token < 0 || second_len->name_token < 0
            || !first->pointer || first_len->pointer
            || !second->pointer || second_len->pointer) {
            continue;
        }
        if (!csane_order_matching(&tokens->tokens[first->name_token],
                                  &tokens->tokens[first_len->name_token])
            || !csane_order_matching(&tokens->tokens[second->name_token],
                                     &tokens->tokens[second_len->name_token])) {
            continue;
        }
        for (int32 j = i; j < i + 4 && compatible; j += 1) {
            for (int32 k = j + 1; k < i + 4; k += 1) {
                if (!csane_order_compatible(file, &members[j], &members[k])) {
                    compatible = false;
                    break;
                }
            }
        }
        if (compatible) {
            csane_rule_issue(file, tokens->tokens[first_len->name_token].offset,
                             STRLIT("STYLE-STRUCT-ARRAY-LENGTH-ORDER"),
                             "group adjacent array pointers before their "
                             "length members");
        }
    }
    ARRAY_FREE(members);
    return;
}

void
csane_member_order_check(CsaneFile *file) {
    Tokenization *tokens = file->tokens;
    TokenRange all = {0, ARRAY_LEN(tokens->tokens)};
    CsaneTokenView view = csane_tokens_full(tokens);
    for (int32 i = 0; i < all.end; i += 1) {
        int32 region;
        int32 open;
        int32 close;
        if (!TOKEN_IS(&tokens->tokens[i], "struct")) {
            continue;
        }
        region = csane_regions_at_token(file->regions, i);
        if (region < 0
            || file->regions->items[region].kind != CSANE_REGION_CODE) {
            continue;
        }
        open = csane_order_next(file, all, i);
        if (open < all.end && tokens->tokens[open].kind == TOKEN_IDENT) {
            open = csane_order_next(file, all, open);
        }
        if (open >= all.end || !TOKEN_IS(&tokens->tokens[open], "{")) {
            continue;
        }
        close = csane_tokens_match(&view, open);
        if (close >= 0) {
            csane_order_struct(file, open, close);
        }
    }
    return;
}

#endif /* CSANECHECK_MEMBER_ORDER_C */
