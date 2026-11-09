// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_INCLUDES_C)
#define CSANECHECK_INCLUDES_C

#include "cbase.h"
#include "csck.h"

/* The pathname remains usable for reading; device/inode tracks physical
 * identity even when different include spellings name the same file. */
int32
csane_include_graph_find(CsaneIncludeGraph *graph, char *path) {
    struct stat st;
    bool valid;

    if (graph == NULL || path == NULL) {
        return -1;
    }
    valid = stat(path, &st) == 0;
    for (int32 i = 0; i < ARRAY_LEN(graph->nodes); i += 1) {
        CsaneIncludeNode *node = &graph->nodes[i];

        if (valid && node->identity_valid
            && node->device == (uint64)st.st_dev
            && node->inode == (uint64)st.st_ino) {
            return i;
        }
        if (!valid && STREQUAL(node->path, node->path_len,
                               path, strlen32(path))) {
            return i;
        }
    }
    return -1;
}

static int32
csane_include_graph_add(CsaneIncludeGraph *graph, char *path) {
    struct stat st;
    CsaneIncludeNode node = {0};
    int32 index = csane_include_graph_find(graph, path);

    if (index >= 0) {
        return index;
    }
    node.path_len = strlen32(path);
    node.path = malloc2((int64)node.path_len + 1);
    memcpy64(node.path, path, node.path_len + 1);
    if (stat(path, &st) == 0) {
        node.identity_valid = true;
        node.device = (uint64)st.st_dev;
        node.inode = (uint64)st.st_ino;
    } else {
        node.uncertain = true;
    }
    index = ARRAY_LEN(graph->nodes);
    ARRAY_PUSH(graph->nodes, node);
    return index;
}

static void
csane_include_graph_scan(CsaneIncludeGraph *graph, int32 index) {
    char *source = NULL;
    int32 length;
    Tokenization tokens;
    CPreprocessorAnalysis pp = {0};
    bool valid;

    graph->nodes[index].scanned = true;
    length = read_entire_file(graph->nodes[index].path, &source);
    if (length < 0) {
        graph->nodes[index].uncertain = true;
        return;
    }
    tokens = tokenize(source, length);
    valid = c_preprocessor_analysis_init(&pp, &tokens);
    if (!valid) {
        graph->nodes[index].uncertain = true;
    }
    for (int32 i = 0; i < ARRAY_LEN(pp.directives); i += 1) {
        CPreprocessorDirective *directive = &pp.directives[i];
        CsaneIncludeEdge edge = {.target = -1};
        Token *argument;
        int32 token_index;
        bool quoted;
        bool c_include;
        char *path;
        int32 name_len;
        struct stat st;

        if (directive->kind != C_PREPROCESSOR_DIRECTIVE_INCLUDE) {
            continue;
        }
        token_index =
            tokenization_next_significant(&tokens, directive->keyword_token);
        if (token_index < 0 || token_index >= directive->tokens.end) {
            graph->nodes[index].uncertain = true;
            continue;
        }
        argument = &tokens.tokens[token_index];
        edge.conditional = pp.token_branches[directive->hash_token] >= 0;
        quoted = argument->kind == TOKEN_LITERAL && argument->len >= 3
                 && argument->text[0] == '"'
                 && argument->text[argument->len - 1] == '"';
        /* Angle-bracket includes use compiler include paths that the
         * checker does not know; macros may expand to a .c source. */
        if (!quoted) {
            if (!TOKEN_IS(argument, "<")) {
                graph->nodes[index].uncertain = true;
            } else {
                bool closed = false;
                int32 start = argument->offset + 1;

                for (int32 j = token_index + 1;
                     j < directive->tokens.end; j += 1) {
                    Token *part = &tokens.tokens[j];
                    int32 angle_len;

                    if (!TOKEN_IS(part, ">")) {
                        continue;
                    }
                    closed = true;
                    angle_len = part->offset - start;
                    if (angle_len >= 2
                        && tokens.text[start + angle_len - 2] == '.'
                        && tokens.text[start + angle_len - 1] == 'c') {
                        graph->nodes[index].uncertain = true;
                    }
                    break;
                }
                if (!closed) {
                    graph->nodes[index].uncertain = true;
                }
            }
            continue;
        }
        name_len = argument->len - 2;
        c_include = name_len >= 2
                    && argument->text[argument->len - 3] == '.'
                    && argument->text[argument->len - 2] == 'c';
        /* Traverse headers too: a header may itself include a .c file. */
        {
            char *including = graph->nodes[index].path;
            int32 base_len = 0;

            if (argument->text[1] != '/') {
                for (int32 j = 0; including[j] != '\0'; j += 1) {
                    if (including[j] == '/') {
                        base_len = j + 1;
                    }
                }
            }
            path = malloc2((int64)base_len + name_len + 1);
            if (base_len > 0) {
                memcpy64(path, including, base_len);
            }
            memcpy64(path + base_len, argument->text + 1, name_len);
            path[base_len + name_len] = '\0';
        }
        if (stat(path, &st) != 0) {
            graph->nodes[index].uncertain = true;
            if (c_include) {
                edge.unresolved = true;
                ARRAY_PUSH(graph->nodes[index].edges, edge);
            }
        } else {
            edge.target = csane_include_graph_add(graph, path);
            ARRAY_PUSH(graph->nodes[index].edges, edge);
        }
        free2(path, (int64)strlen32(path) + 1);
    }
    c_preprocessor_analysis_free(&pp);
    free_tokenization(&tokens);
    free2(source, (int64)length + 1);
    return;
}

bool
csane_include_graph_build(CsaneIncludeGraph *graph, CsanePaths *roots) {
    if (graph == NULL || roots == NULL) {
        return false;
    }
    *graph = (CsaneIncludeGraph){0};
    for (int32 i = 0; i < strflex_list_len(&roots->strings); i += 1) {
        StrFlex *path = roots->strings.items[i];

        csane_include_graph_add(graph, path->data);
    }
    /* A growable queue of node indices is implicit in the array. Marking
     * each node scanned before discovering its edges terminates cycles. */
    for (int32 i = 0; i < ARRAY_LEN(graph->nodes); i += 1) {
        if (!graph->nodes[i].scanned) {
            csane_include_graph_scan(graph, i);
        }
    }
    return true;
}

void
csane_include_graph_free(CsaneIncludeGraph *graph) {
    if (graph == NULL) {
        return;
    }
    for (int32 i = 0; i < ARRAY_LEN(graph->nodes); i += 1) {
        CsaneIncludeNode *node = &graph->nodes[i];

        ARRAY_FREE(node->edges);
        free2(node->path, (int64)node->path_len + 1);
    }
    ARRAY_FREE(graph->nodes);
    return;
}

/* Reachability has separate states for unconditional and potentially
 * conditional paths. Unresolved dependencies are retained as uncertainty. */
CsaneIncludeReachability
csane_include_graph_reach(CsaneIncludeGraph *graph, int32 from, int32 to) {
    CsaneIncludeReachability result = {0};
    int32 *queue = NULL;
    uint8 *seen;
    int32 head = 0;
    int32 count;

    if (graph == NULL || from < 0 || to < 0
        || from >= ARRAY_LEN(graph->nodes)
        || to >= ARRAY_LEN(graph->nodes)) {
        return result;
    }
    count = ARRAY_LEN(graph->nodes);
    seen = malloc2(2*count*SIZEOF(*seen));
    memset64(seen, 0, 2*count*SIZEOF(*seen));
    ARRAY_PUSH(queue, from*2);
    seen[from*2] = 1;
    while (head < ARRAY_LEN(queue)) {
        int32 state = queue[head];
        int32 node_id = state / 2;
        bool conditional = (state % 2) != 0;
        CsaneIncludeNode *node = &graph->nodes[node_id];

        head += 1;
        if (node->uncertain) {
            result.uncertain = true;
        }
        for (int32 i = 0; i < ARRAY_LEN(node->edges); i += 1) {
            CsaneIncludeEdge *edge = &node->edges[i];
            int32 next;
            bool edge_conditional = conditional || edge->conditional;

            if (edge->unresolved || edge->target < 0) {
                result.uncertain = true;
                continue;
            }
            if (edge->target == to) {
                result.reachable = true;
                if (!edge_conditional) {
                    result.unconditional = true;
                }
            }
            next = edge->target*2 + edge_conditional;
            if (!seen[next]) {
                seen[next] = 1;
                ARRAY_PUSH(queue, next);
            }
        }
    }
    free2(seen, 2*count*SIZEOF(*seen));
    ARRAY_FREE(queue);
    return result;
}

#endif /* CSANECHECK_INCLUDES_C */
