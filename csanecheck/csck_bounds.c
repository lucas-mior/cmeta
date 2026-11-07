// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_BOUNDS_C)
#define CSANECHECK_BOUNDS_C

#include "cbase.h"
#include "csck.h"
#include "csck_tokens.h"

static TokenRange
csane_bounds_unwrap(CsaneFile *file, TokenRange range) {
    CsaneTokenView view = csane_tokens_full(file->tokens);
    return csane_tokens_unwrap(&view, range);
}

static bool
csane_bounds_token(CsaneFile *file, int32 index, char *name, int32 name_len) {
    return index >= 0 && index < ARRAY_LEN(file->tokens->tokens)
           && TOKEN_IS(&file->tokens->tokens[index], name, name_len);
}




static bool
csane_bounds_call(CsaneFile *file, TokenRange range,
                  char *name, int32 name_len, TokenRange *args) {
    int32 first;
    int32 open;
    CsaneTokenView view;
    int32 close;

    range = csane_bounds_unwrap(file, range);
    first = token_range_first_significant(file->tokens, range);
    if (first >= range.end
        || !csane_bounds_token(file, first, name, name_len)) {
        return false;
    }
    open = tokenization_next_significant(file->tokens, first);
    view = csane_tokens_full(file->tokens);
    if (open >= range.end || !csane_bounds_token(file, open, STRLIT("("))) {
        return false;
    }
    close = csane_tokens_match(&view, open);
    if (close < 0 || close >= range.end
        || tokenization_next_significant(file->tokens, close) < range.end) {
        return false;
    }
    return csane_tokens_arguments(&view, open, args, 2) == 2;
}

/* Check the source gap rather than the statement tree. A macro replacement
 * may contain legitimate assertion statements without CsaneStatement nodes.
 * Comments are trivia, and a macro continuation is permitted before the one
 * required physical newline. Other intervening code breaks adjacency. */


void
csane_bounds_check(CsaneFile *file) {
    if (file->is_ryu || file->regions == NULL) {
        return;
    }
    for (int32 tok_i = 0; tok_i < ARRAY_LEN(file->tokens->tokens); tok_i += 1) {
        do {
            bool _i1_predicate;
            bool _i4_result;
            bool _i4_predicate;
            bool _i2_predicate;
            int32 _i5_first = tok_i;

            Tokenization *tokens = file->tokens;
            CsaneTokenView view;
            int32 open;
            int32 close;
            TokenRange lower[2];
            int32 semicolon;
            SourceLocation end;
            SourceLocation semi;
            int32 region;
            bool macro;
            int32 upper;
            TokenRange upper_args[2];

            if (!csane_bounds_token(file, _i5_first, STRLIT("ASSERT_GE"))) {
                goto _i5_done;
            }
            view = csane_tokens_full(tokens);
            open = csane_tokens_next(&view, _i5_first);
            if (!csane_bounds_token(file, open, STRLIT("("))) {
                goto _i5_done;
            }
            close = csane_tokens_match(&view, open);
            if (close < 0) {
                goto _i5_done;
            }
            _i2_predicate =
                !csane_bounds_call(file, (TokenRange){_i5_first, close + 1},
                                   STRLIT("ASSERT_GE"), lower);
            if (!_i2_predicate) {
                bool _i3_result;
                do {
                    TokenRange _i3_range = lower[1];

                    int32 first;

                    _i3_range = csane_bounds_unwrap(file, _i3_range);
                    first = token_range_first_significant(file->tokens,
                                                          _i3_range);
                    _i3_result = false;
                    if (first < _i3_range.end
                        && csane_bounds_token(file, first, STRLIT("0"))) {
                        int32 next = tokenization_next_significant(file->tokens,
                                                                   first);
                        _i3_result = next >= _i3_range.end;
                    }
                    goto _i3_done;
                    _i3_done: ;
                } while (false);
                _i2_predicate = !_i3_result;
            }
            if (!_i2_predicate) {
                bool _i2_result;
                do {
                    TokenRange _i2_range = lower[0];

                    CsaneTokenView nested_view;
                    int32 i;

                    _i2_range = csane_bounds_unwrap(file, _i2_range);
                    i = token_range_first_significant(file->tokens, _i2_range);
                    if (i >= _i2_range.end
                        || file->tokens->tokens[i].kind != TOKEN_IDENT) {
                        _i2_result = false;
                        goto _i2_done;
                    }
                    i = tokenization_next_significant(file->tokens, i);
                    nested_view = csane_tokens_full(file->tokens);
                    while (i < _i2_range.end) {
                        int32 nested_close;
                        int32 first;
                        Token *index;

                        if (csane_bounds_token(file, i, STRLIT("."))
                            || csane_bounds_token(file, i, STRLIT("->"))) {
                            i = tokenization_next_significant(file->tokens, i);
                            if (i >= _i2_range.end
                                || file->tokens->tokens[i].kind
                                    != TOKEN_IDENT) {
                                _i2_result = false;
                                goto _i2_done;
                            }
                            i = tokenization_next_significant(file->tokens, i);
                            continue;
                        }
                        if (!csane_bounds_token(file, i, STRLIT("["))) {
                            _i2_result = false;
                            goto _i2_done;
                        }
                        nested_close = csane_tokens_match(&nested_view, i);
                        if (nested_close < 0 || nested_close >= _i2_range.end) {
                            _i2_result = false;
                            goto _i2_done;
                        }
                        first = tokenization_next_significant(file->tokens, i);
                        if (first >= nested_close
                            || tokenization_next_significant(file->tokens,
                                                             first)
                                              != nested_close) {
                            _i2_result = false;
                            goto _i2_done;
                        }
                        index = &file->tokens->tokens[first];
                        if (index->kind != TOKEN_IDENT
                            && index->kind != TOKEN_LITERAL) {
                            _i2_result = false;
                            goto _i2_done;
                        }
                        if (index->kind == TOKEN_LITERAL
                            && (index->len == 0 || index->text[0] < '0'
                                || index->text[0] > '9')) {
                            _i2_result = false;
                            goto _i2_done;
                        }
                        i = tokenization_next_significant(file->tokens,
                                                          nested_close);
                    }
                    _i2_result = true;
                    goto _i2_done;
                    _i2_done: ;
                } while (false);
                _i2_predicate = !_i2_result;
            }
            if (_i2_predicate) {
                goto _i5_done;
            }
            semicolon = csane_tokens_next(&view, close);
            if (!csane_bounds_token(file, semicolon, STRLIT(";"))) {
                goto _i5_done;
            }
            /* The Perl rule requires the semicolon on the closing-call line. */
            end = tokenization_token_location(tokens, close);
            semi = tokenization_token_location(tokens, semicolon);
            if (end.line != semi.line) {
                goto _i5_done;
            }
            region = csane_regions_at_token(file->regions, _i5_first);
            if (region < 0) {
                goto _i5_done;
            }
            macro = csane_regions_enclosing(file->regions, region,
                                            CSANE_REGION_MACRO_REPLACEMENT)
                                                >= 0;
            upper = csane_tokens_next(&view, semicolon);
            if (macro && csane_bounds_token(file, upper, STRLIT("\\"))) {
                upper = csane_tokens_next(&view, upper);
            }
            if (!csane_bounds_token(file, upper, STRLIT("ASSERT_LT"))
                || region != csane_regions_at_token(file->regions, upper)) {
                goto _i5_done;
            }
            do {
                int32 _i4_next = upper;

                Tokenization *nested_tokens = file->tokens;
                Token *last = &nested_tokens->tokens[semicolon];
                Token *first = &nested_tokens->tokens[_i4_next];
                SourceLocation a = tokenization_token_location(nested_tokens,
                                                               semicolon);
                SourceLocation b = tokenization_token_location(nested_tokens,
                                                               _i4_next);
                int32 cursor;
                int32 nested_end;
                int32 newlines = 0;

                if (b.line != a.line + 1) {
                    _i4_result = false;
                    goto _i4_done;
                }
                cursor = last->offset + last->len;
                nested_end = first->offset;
                for (int32 i = semicolon + 1; i < _i4_next; i += 1) {
                    Token *token = &nested_tokens->tokens[i];
                    if (token->offset < cursor || token->offset >= nested_end) {
                        continue;
                    }
                    if (token->kind != TOKEN_COMMENT && !token_is_trivia(token)
                        && !(macro && TOKEN_IS(token, "\\"))) {
                        _i4_result = false;
                        goto _i4_done;
                    }
                    if (token->kind == TOKEN_COMMENT) {
                        cursor = token->offset + token->len;
                    }
                }
                for (int32 i =
                    last->offset + last->len; i < nested_end; i += 1) {
                    if (nested_tokens->text[i] == '\n') {
                        newlines += 1;
                    }
                }
                _i4_result = newlines == 1;
                goto _i4_done;
                _i4_done: ;
            } while (false);
            _i4_predicate = !_i4_result;
            if (_i4_predicate) {
                goto _i5_done;
            }
            open = csane_tokens_next(&view, upper);
            if (!csane_bounds_token(file, open, STRLIT("("))) {
                goto _i5_done;
            }
            close = csane_tokens_match(&view, open);
            if (close < 0) {
                goto _i5_done;
            }
            _i1_predicate = !csane_bounds_call(file,
                                               (TokenRange){upper, close + 1},
                                               STRLIT("ASSERT_LT"),
                                               upper_args);
            if (!_i1_predicate) {
                bool _i1_result;
                do {
                    TokenRange _i1_a = lower[0];
                    TokenRange _i1_b = upper_args[0];

                    int32 i;
                    int32 j;

                    _i1_a = csane_bounds_unwrap(file, _i1_a);
                    _i1_b = csane_bounds_unwrap(file, _i1_b);
                    i = token_range_first_significant(file->tokens, _i1_a);
                    j = token_range_first_significant(file->tokens, _i1_b);
                    while (i < _i1_a.end && j < _i1_b.end) {
                        Token *lhs = &file->tokens->tokens[i];
                        Token *rhs = &file->tokens->tokens[j];
                        if (lhs->len != rhs->len
                            || memcmp64(lhs->text, rhs->text, lhs->len)) {
                            _i1_result = false;
                            goto _i1_done;
                        }
                        i = tokenization_next_significant(file->tokens, i);
                        j = tokenization_next_significant(file->tokens, j);
                    }
                    _i1_result = i >= _i1_a.end && j >= _i1_b.end;
                    goto _i1_done;
                    _i1_done: ;
                } while (false);
                _i1_predicate = !_i1_result;
            }
            if (!_i1_predicate) {
                _i1_predicate = token_range_first_significant(tokens,
                                                              upper_args[1])
                   >= upper_args[1].end;
            }
            if (!_i1_predicate) {
                _i1_predicate = !csane_bounds_token(file,
                                                    csane_tokens_next(&view,
                                                                      close),
                                                    STRLIT(";"));
            }
            if (_i1_predicate) {
                goto _i5_done;
            }
            semicolon = csane_tokens_next(&view, close);
            end = tokenization_token_location(tokens, close);
            semi = tokenization_token_location(tokens, semicolon);
            if (end.line != semi.line) {
                goto _i5_done;
            }
            csane_rule_issue(file, tokens->tokens[_i5_first].offset,
                             STRLIT("STYLE-ASSERT-BOUNDS"),
                             "use ASSERT_BETWEEN for adjacent bounds");
            _i5_done: ;
        } while (false);
    }
}

#endif /* CSANECHECK_BOUNDS_C */
