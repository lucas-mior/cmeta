// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_REGIONS_C)
#define CSANECHECK_REGIONS_C

#include "cbase.h"
#include "csck_regions.h"

static TokenRange
csane_regions_token_range(CsaneRegions *regions, SourceRange source) {
    Tokenization *tokens = regions->tokens;
    return (TokenRange){
        tokenization_token_at_or_after_offset(tokens, source.start),
        tokenization_token_at_or_after_offset(tokens, source.end),
    };
}

static int32
csane_regions_append(CsaneRegions *regions, int32 parent,
                     enum CsaneRegionKind kind, SourceRange source,
                     int32 directive, int32 group, int32 branch) {
    int32 index = regions->count;
    if (index == regions->capacity) {
        int32 next = regions->capacity ? regions->capacity*2 : 32;
        regions->items = realloc2(regions->items, regions->capacity,
                                  next, SIZEOF(*regions->items));
        regions->capacity = next;
    }
    regions->items[index] = (CsaneSourceRegion){
        .kind = kind,
        .source = source,
        .tokens = csane_regions_token_range(regions, source),
        .parent = parent,
        .first_child = -1,
        .last_child = -1,
        .next_sibling = -1,
        .directive_index = directive,
        .group_index = group,
        .branch_index = branch,
        .complete = true,
    };
    regions->count += 1;
    if (parent >= 0) {
        CsaneSourceRegion *ancestor = &regions->items[parent];
        if (ancestor->last_child < 0) {
            ancestor->first_child = index;
        } else {
            regions->items[ancestor->last_child].next_sibling = index;
        }
        ancestor->last_child = index;
    }
    return index;
}

static void
csane_regions_text(CsaneRegions *regions, int32 parent,
                   enum CsaneRegionKind kind, int32 start, int32 end) {
    if (start < end) {
        csane_regions_append(regions, parent, kind,
                             (SourceRange){start, end}, -1, -1, -1);
    }
    return;
}

static void
csane_regions_code(CsaneRegions *regions, int32 parent,
                   int32 start, int32 end) {
    csane_regions_text(regions, parent, CSANE_REGION_CODE, start, end);
    return;
}

/* Macro parameter lists and replacement bytes are distinct regions, but
 * text before, between and after them remains part of the directive. */
static void
csane_regions_define(CsaneRegions *regions, int32 parent,
                     CPreprocessorDefine *define) {
    Tokenization *tokens = regions->tokens;
    SourceRange source = regions->items[parent].source;
    int32 cursor = source.start;
    if (define->function_like) {
        Token *open = &tokens->tokens[define->open_paren_token];
        Token *close = &tokens->tokens[define->close_paren_token];
        SourceRange parameters = {open->offset, close->offset + close->len};
        csane_regions_text(regions, parent, CSANE_REGION_DIRECTIVE_TEXT,
                           cursor, parameters.start);
        csane_regions_append(regions, parent, CSANE_REGION_MACRO_PARAMETERS,
                             parameters, -1, -1, -1);
        cursor = parameters.end;
    }
    SourceRange replacement = define->replacement_source;
    if (replacement.start < cursor) {
        replacement.start = cursor;
    }
    csane_regions_text(regions, parent, CSANE_REGION_DIRECTIVE_TEXT,
                       cursor, replacement.start);
    if (replacement.start < replacement.end) {
        csane_regions_append(regions, parent,
                             CSANE_REGION_MACRO_REPLACEMENT,
                             replacement, -1, -1, -1);
    }
    csane_regions_text(regions, parent, CSANE_REGION_DIRECTIVE_TEXT,
                       replacement.end, source.end);
    return;
}

/* A syntax problem belongs to its directive, not every code region. */
static bool
csane_regions_directive_complete(CsaneRegions *regions, int32 directive) {
    CPreprocessorAnalysis *pp = regions->preprocessor;
    for (int32 i = 0; i < pp->issue_count; i += 1) {
        if (pp->issues[i].directive_index == directive) {
            return false;
        }
    }
    return true;
}

static void
csane_regions_directive(CsaneRegions *regions, int32 parent,
                        int32 directive_index) {
    CPreprocessorDirective *directive =
        &regions->preprocessor->directives[directive_index];
    CPreprocessorDefine define = {0};
    bool is_define = directive->kind == C_PREPROCESSOR_DIRECTIVE_DEFINE;
    int32 index = csane_regions_append(regions, parent,
                                       is_define ? CSANE_REGION_MACRO_DEFINE
                                                 : CSANE_REGION_DIRECTIVE,
                                       directive->source, directive_index,
                                       -1, -1);
    regions->items[index].complete =
        csane_regions_directive_complete(regions, directive_index);
    if (is_define) {
        if (c_preprocessor_define_info(regions->tokens, directive, &define)) {
            csane_regions_define(regions, index, &define);
        } else {
            /* A malformed parameter list is opaque, but does not affect
             * subsequent directives or neighboring conditional branches. */
            regions->items[index].complete = false;
            csane_regions_text(regions, index, CSANE_REGION_DIRECTIVE_TEXT,
                               directive->source.start, directive->source.end);
        }
    }
    return;
}

/* Directives are sorted by source offset. Binary search prevents a deep
 * conditional nest from repeatedly scanning every preceding directive. */
static int32
csane_regions_first_directive(CsaneRegions *regions, int32 offset) {
    CPreprocessorAnalysis *pp = regions->preprocessor;
    int32 first = 0;
    int32 end = pp->directive_count;
    while (first < end) {
        int32 middle = first + (end - first)/2;
        if (pp->directives[middle].source.start < offset) {
            first = middle + 1;
        } else {
            end = middle;
        }
    }
    return first;
}

/* The preprocessor retains usable groups even after a syntax error.
 * Reject an inconsistent group locally rather than disabling every group. */
static bool
csane_regions_group_usable(CsaneRegions *regions, int32 index) {
    CPreprocessorAnalysis *pp = regions->preprocessor;
    if (index < 0 || index >= pp->group_count) {
        return false;
    }
    CPreprocessorGroup *group = &pp->groups[index];
    if (group->first_branch < 0 || group->first_branch >= pp->branch_count
        || group->source.start < 0
        || group->source.end > regions->tokens->text_len
        || group->source.end <= group->source.start) {
        return false;
    }
    int32 cursor = group->source.start;
    for (int32 b = group->first_branch; b >= 0;
         b = pp->branches[b].next_branch) {
        if (b >= pp->branch_count || pp->branches[b].group_index != index) {
            return false;
        }
        CPreprocessorBranch *branch = &pp->branches[b];
        if (branch->directive_index < 0
            || branch->directive_index >= pp->directive_count) {
            return false;
        }
        SourceRange directive = pp->directives[branch->directive_index].source;
        if (directive.start < cursor
            || branch->body_source.start != directive.end
            || branch->body_source.end < branch->body_source.start
            || branch->body_source.end > group->source.end) {
            return false;
        }
        cursor = branch->body_source.end;
    }
    if (group->endif_directive >= 0) {
        if (group->endif_directive >= pp->directive_count
            || pp->directives[group->endif_directive].source.start < cursor
            || pp->directives[group->endif_directive].source.end
               != group->source.end) {
            return false;
        }
    } else if (cursor != group->source.end) {
        return false;
    }
    return true;
}

static void csane_regions_content(CsaneRegions *, int32, SourceRange,
                                  int32 *, int32);

static void
csane_regions_group(CsaneRegions *regions, int32 parent,
                    int32 group_index, int32 *opening_groups) {
    CPreprocessorAnalysis *pp = regions->preprocessor;
    CPreprocessorGroup *group = &pp->groups[group_index];
    int32 region = csane_regions_append(regions, parent,
                                        CSANE_REGION_CONDITIONAL_GROUP,
                                        group->source, -1, group_index, -1);
    regions->items[region].complete = group->endif_directive >= 0;
    int32 cursor = group->source.start;
    for (int32 b = group->first_branch; b >= 0;
         b = pp->branches[b].next_branch) {
        CPreprocessorBranch *branch = &pp->branches[b];
        CPreprocessorDirective *directive =
            &pp->directives[branch->directive_index];
        SourceRange source = {
            directive->source.start,
            branch->body_source.end,
        };
        int32 branch_region;
        csane_regions_code(regions, region, cursor, source.start);
        branch_region = csane_regions_append(regions, region,
                                             CSANE_REGION_CONDITIONAL_BRANCH,
                                             source, -1, group_index, b);
        CsaneSourceRegion *current = &regions->items[branch_region];
        current->complete = group->endif_directive >= 0
                            || branch->next_branch >= 0;
        csane_regions_directive(regions, branch_region,
                                branch->directive_index);
        csane_regions_content(regions, branch_region, branch->body_source,
                              opening_groups, b);
        cursor = source.end;
    }
    if (group->endif_directive >= 0) {
        CPreprocessorDirective *closing =
            &pp->directives[group->endif_directive];
        csane_regions_code(regions, region, cursor, closing->source.start);
        csane_regions_directive(regions, region, group->endif_directive);
        cursor = closing->source.end;
    }
    csane_regions_code(regions, region, cursor, group->source.end);
    return;
}

static void
csane_regions_content(CsaneRegions *regions, int32 parent,
                      SourceRange source, int32 *opening_groups,
                      int32 parent_branch) {
    CPreprocessorAnalysis *pp = regions->preprocessor;
    int32 cursor = source.start;
    int32 i = csane_regions_first_directive(regions, cursor);
    while (i < pp->directive_count
           && pp->directives[i].source.start < source.end) {
        CPreprocessorDirective *directive = &pp->directives[i];
        int32 group = opening_groups[i];
        if (directive->source.start < cursor) {
            i += 1;
            continue;
        }
        csane_regions_code(regions, parent, cursor, directive->source.start);
        if (group >= 0 && pp->groups[group].parent_branch == parent_branch
            && csane_regions_group_usable(regions, group)
            && pp->groups[group].source.end <= source.end) {
            csane_regions_group(regions, parent, group, opening_groups);
            cursor = pp->groups[group].source.end;
            i = csane_regions_first_directive(regions, cursor);
        } else {
            csane_regions_directive(regions, parent, i);
            cursor = directive->source.end;
            i += 1;
        }
    }
    csane_regions_code(regions, parent, cursor, source.end);
    return;
}

bool
csane_regions_init(CsaneRegions *regions, Tokenization *tokens,
                   CPreprocessorAnalysis *pp) {
    int32 *opening_groups = NULL;
    if (regions == NULL) {
        return false;
    }
    *regions = (CsaneRegions){0};
    if (tokens == NULL || pp == NULL || pp->tokenization != tokens
        || tokens->text == NULL || tokens->text_len < 0
        || tokens->token_count < 0) {
        return false;
    }
    regions->tokens = tokens;
    regions->preprocessor = pp;
    regions->complete = pp->issue_count == 0;
    int32 root = csane_regions_append(regions, -1, CSANE_REGION_FILE,
                                      (SourceRange){0, tokens->text_len},
                                      -1, -1, -1);
    if (pp->directive_count > 0) {
        opening_groups = malloc2((int64)pp->directive_count
                                 *SIZEOF(*opening_groups));
        for (int32 i = 0; i < pp->directive_count; i += 1) {
            opening_groups[i] = -1;
        }
        /* Partial conditional groups survive unrelated syntax issues. */
        for (int32 i = 0; i < pp->group_count; i += 1) {
            if (!csane_regions_group_usable(regions, i)) {
                continue;
            }
            int32 branch = pp->groups[i].first_branch;
            int32 directive = pp->branches[branch].directive_index;
            opening_groups[directive] = i;
        }
    }
    csane_regions_content(regions, root, (SourceRange){0, tokens->text_len},
                          opening_groups, -1);
    free2(opening_groups, (int64)pp->directive_count
                          *SIZEOF(*opening_groups));
    return true;
}

void
csane_regions_free(CsaneRegions *regions) {
    if (regions == NULL) {
        return;
    }
    free2(regions->items, (int64)regions->capacity*SIZEOF(*regions->items));
    *regions = (CsaneRegions){0};
    return;
}

int32
csane_regions_at_offset(CsaneRegions *regions, int32 offset) {
    if (regions == NULL || regions->count == 0 || offset < 0
        || offset >= regions->tokens->text_len) {
        return -1;
    }
    int32 region = 0;
    while (regions->items[region].first_child >= 0) {
        int32 child = regions->items[region].first_child;
        while (child >= 0) {
            SourceRange source = regions->items[child].source;
            if (offset >= source.start && offset < source.end) {
                break;
            }
            child = regions->items[child].next_sibling;
        }
        if (child < 0) {
            break;
        }
        region = child;
    }
    return region;
}

int32
csane_regions_at_token(CsaneRegions *regions, int32 token) {
    if (regions == NULL || regions->tokens == NULL || token < 0
        || token >= regions->tokens->token_count) {
        return -1;
    }
    return csane_regions_at_offset(regions,
                                   regions->tokens->tokens[token].offset);
}

int32
csane_regions_enclosing(CsaneRegions *regions, int32 region,
                        enum CsaneRegionKind kind) {
    if (regions == NULL || region < 0 || region >= regions->count) {
        return -1;
    }
    while (region >= 0) {
        if (regions->items[region].kind == kind) {
            return region;
        }
        region = regions->items[region].parent;
    }
    return -1;
}

#endif /* CSANECHECK_REGIONS_C */
