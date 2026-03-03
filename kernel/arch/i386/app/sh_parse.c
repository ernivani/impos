#include <kernel/sh_parse.h>
#include <kernel/env.h>
#include <kernel/task.h>
#include <kernel/pipe.h>
#include <kernel/signal.h>
#include <kernel/fs.h>
#include <kernel/glob.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ═══ Exit code tracking ═══════════════════════════════════════ */

static int sh_last_exit = 0;

int sh_get_exit_code(void) { return sh_last_exit; }
void sh_set_exit_code(int code) { sh_last_exit = code; }

/* ═══ Tokenizer ════════════════════════════════════════════════ */

static int is_operator_char(char c) {
    return c == '|' || c == '&' || c == ';' || c == '>' ||
           c == '<' || c == '(' || c == ')';
}

int sh_tokenize(const char *input, sh_token_t *tokens, int max_tokens) {
    if (!input || !tokens || max_tokens <= 0)
        return -1;

    int count = 0;
    const char *p = input;

    while (*p && count < max_tokens - 1) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p || *p == '#')
            break;  /* end of input or comment */

        sh_token_t *tok = &tokens[count];
        memset(tok, 0, sizeof(*tok));

        /* Check for operators */
        if (*p == '|') {
            if (p[1] == '|') {
                tok->type = TOK_OR;
                strcpy(tok->value, "||");
                p += 2;
            } else {
                tok->type = TOK_PIPE;
                strcpy(tok->value, "|");
                p++;
            }
            count++;
            continue;
        }
        if (*p == '&') {
            if (p[1] == '&') {
                tok->type = TOK_AND;
                strcpy(tok->value, "&&");
                p += 2;
            } else {
                tok->type = TOK_BG;
                strcpy(tok->value, "&");
                p++;
            }
            count++;
            continue;
        }
        if (*p == ';') {
            tok->type = TOK_SEMI;
            strcpy(tok->value, ";");
            p++;
            count++;
            continue;
        }
        if (*p == '(') {
            tok->type = TOK_LPAREN;
            strcpy(tok->value, "(");
            p++;
            count++;
            continue;
        }
        if (*p == ')') {
            tok->type = TOK_RPAREN;
            strcpy(tok->value, ")");
            p++;
            count++;
            continue;
        }
        if (*p == '>') {
            if (p[1] == '>') {
                tok->type = TOK_REDIR_APP;
                strcpy(tok->value, ">>");
                p += 2;
            } else {
                tok->type = TOK_REDIR_OUT;
                strcpy(tok->value, ">");
                p++;
            }
            count++;
            continue;
        }
        if (*p == '<') {
            tok->type = TOK_REDIR_IN;
            strcpy(tok->value, "<");
            p++;
            count++;
            continue;
        }
        /* 2> stderr redirection */
        if (*p == '2' && p[1] == '>') {
            tok->type = TOK_REDIR_ERR;
            strcpy(tok->value, "2>");
            p += 2;
            /* skip optional space after 2> */
            count++;
            continue;
        }

        /* Word token — handles quoting and escaping */
        tok->type = TOK_WORD;
        int vpos = 0;
        while (*p && vpos < 255) {
            if (*p == '\\' && p[1]) {
                /* Backslash escape: take next char literally */
                p++;
                tok->value[vpos++] = *p++;
                continue;
            }
            if (*p == '\'') {
                /* Single quotes: literal, no expansion */
                p++; /* skip opening quote */
                while (*p && *p != '\'' && vpos < 255) {
                    tok->value[vpos++] = *p++;
                }
                if (*p == '\'') p++; /* skip closing quote */
                continue;
            }
            if (*p == '"') {
                /* Double quotes: allow $VAR expansion (done later in expand phase) */
                p++; /* skip opening quote */
                while (*p && *p != '"' && vpos < 255) {
                    if (*p == '\\' && p[1] && (p[1] == '"' || p[1] == '\\' || p[1] == '$')) {
                        p++;
                        tok->value[vpos++] = *p++;
                    } else {
                        tok->value[vpos++] = *p++;
                    }
                }
                if (*p == '"') p++; /* skip closing quote */
                continue;
            }
            /* End of word on whitespace or operator char */
            if (*p == ' ' || *p == '\t' || is_operator_char(*p))
                break;
            /* Check for 2> in the middle of a word - not a word char */
            if (*p == '2' && p[1] == '>')
                break;
            tok->value[vpos++] = *p++;
        }
        tok->value[vpos] = '\0';
        if (vpos > 0)
            count++;
    }

    /* Terminate with TOK_EOF */
    tokens[count].type = TOK_EOF;
    tokens[count].value[0] = '\0';

    return count;
}

/* ═══ Variable Expansion ═══════════════════════════════════════ */

int sh_expand(const char *input, char *output, size_t output_size) {
    if (!input || !output || output_size == 0)
        return -1;

    size_t out_pos = 0;
    const char *p = input;

    while (*p && out_pos < output_size - 1) {
        if (*p == '~' && (p == input) &&
            (p[1] == '\0' || p[1] == '/')) {
            /* ~ expansion at start of word */
            const char *home = env_get("HOME");
            if (home) {
                while (*home && out_pos < output_size - 1)
                    output[out_pos++] = *home++;
            }
            p++; /* skip ~ */
            continue;
        }
        if (*p == '$') {
            p++; /* skip $ */

            /* Special variables */
            if (*p == '?') {
                /* $? — last exit code */
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", sh_last_exit);
                for (int i = 0; buf[i] && out_pos < output_size - 1; i++)
                    output[out_pos++] = buf[i];
                p++;
                continue;
            }
            if (*p == '$') {
                /* $$ — current PID */
                char buf[16];
                int tid = task_get_current();
                task_info_t *t = task_get(tid);
                snprintf(buf, sizeof(buf), "%d", t ? t->pid : 0);
                for (int i = 0; buf[i] && out_pos < output_size - 1; i++)
                    output[out_pos++] = buf[i];
                p++;
                continue;
            }
            if (*p == '0' && (p[1] == '\0' || p[1] == ' ' || p[1] == '/' ||
                              p[1] == '$' || p[1] == '"' || p[1] == ':')) {
                /* $0 — shell name */
                const char *name = "sh";
                while (*name && out_pos < output_size - 1)
                    output[out_pos++] = *name++;
                p++;
                continue;
            }
            if (*p == '(') {
                /* $() command substitution */
                p++; /* skip ( */
                /* Find matching ) */
                int depth = 1;
                const char *start = p;
                while (*p && depth > 0) {
                    if (*p == '(') depth++;
                    else if (*p == ')') depth--;
                    if (depth > 0) p++;
                }
                int cmd_len = p - start;
                if (*p == ')') p++; /* skip closing ) */

                /* Build the inner command */
                char inner_cmd[256];
                if (cmd_len >= (int)sizeof(inner_cmd))
                    cmd_len = sizeof(inner_cmd) - 1;
                memcpy(inner_cmd, start, cmd_len);
                inner_cmd[cmd_len] = '\0';

                /* Execute with output capture via shell pipe mode */
                extern void shell_pipe_putchar(char c);
                extern int shell_is_pipe_mode(void);
                extern int shell_pipe_len;
                extern char shell_pipe_buf[];
                extern int shell_pipe_mode;

                int old_mode = shell_pipe_mode;
                int old_len = shell_pipe_len;

                shell_pipe_mode = 1;
                shell_pipe_len = 0;

                /* Recursively process the inner command */
                extern void shell_process_command(char *command);
                shell_process_command(inner_cmd);

                shell_pipe_buf[shell_pipe_len] = '\0';

                /* Copy captured output, stripping trailing newline */
                int copy_len = shell_pipe_len;
                while (copy_len > 0 && shell_pipe_buf[copy_len - 1] == '\n')
                    copy_len--;
                for (int i = 0; i < copy_len && out_pos < output_size - 1; i++)
                    output[out_pos++] = shell_pipe_buf[i];

                shell_pipe_mode = old_mode;
                shell_pipe_len = old_len;
                continue;
            }

            /* Handle ${VAR} or $VAR */
            int use_braces = 0;
            if (*p == '{') {
                use_braces = 1;
                p++;
            }

            /* Extract variable name */
            char var_name[64];
            int name_pos = 0;
            while (*p && name_pos < 63) {
                if (use_braces && *p == '}') {
                    p++;
                    break;
                }
                if (!use_braces) {
                    /* End on non-identifier chars */
                    if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                          (*p >= '0' && *p <= '9') || *p == '_'))
                        break;
                }
                var_name[name_pos++] = *p++;
            }
            var_name[name_pos] = '\0';

            const char *value = env_get(var_name);
            if (value) {
                while (*value && out_pos < output_size - 1)
                    output[out_pos++] = *value++;
            }
            continue;
        }
        output[out_pos++] = *p++;
    }

    output[out_pos] = '\0';
    return 0;
}

/* ═══ Parser ═══════════════════════════════════════════════════ */

/* Static pool of AST nodes to avoid malloc for small parse trees */
#define SH_NODE_POOL_SIZE 32
static sh_node_t node_pool[SH_NODE_POOL_SIZE];
static int node_pool_next = 0;

static sh_node_t *sh_alloc_node(void) {
    if (node_pool_next >= SH_NODE_POOL_SIZE)
        return NULL;
    sh_node_t *n = &node_pool[node_pool_next++];
    memset(n, 0, sizeof(*n));
    return n;
}

static void sh_reset_pool(void) {
    node_pool_next = 0;
}

/* Token stream cursor for recursive descent */
static sh_token_t *tok_stream;
static int tok_pos;
static int tok_total;

static sh_token_t *tok_peek(void) {
    if (tok_pos < tok_total)
        return &tok_stream[tok_pos];
    /* Return EOF sentinel */
    static sh_token_t eof = { TOK_EOF, "" };
    return &eof;
}

static sh_token_t *tok_next(void) {
    sh_token_t *t = tok_peek();
    if (tok_pos < tok_total)
        tok_pos++;
    return t;
}

static int tok_is(sh_token_type_t type) {
    return tok_peek()->type == type;
}

static int tok_is_word(const char *word) {
    return tok_peek()->type == TOK_WORD && strcmp(tok_peek()->value, word) == 0;
}

/* Forward declarations for recursive descent */
static sh_node_t *parse_list(void);
static sh_node_t *parse_pipeline(void);
static sh_node_t *parse_command(void);
/* parse_compound() not needed — parse_command() handles compound directly */
static sh_node_t *parse_if(void);
static sh_node_t *parse_for(void);
static sh_node_t *parse_while(int is_until);
static sh_node_t *parse_case(void);

/* Parse a simple command: words + redirections, stopping at operators */
static sh_node_t *parse_simple_command(void) {
    sh_node_t *node = sh_alloc_node();
    if (!node) return NULL;
    node->type = SH_CMD;

    while (!tok_is(TOK_EOF) && !tok_is(TOK_PIPE) && !tok_is(TOK_AND) &&
           !tok_is(TOK_OR) && !tok_is(TOK_SEMI) && !tok_is(TOK_BG) &&
           !tok_is(TOK_RPAREN) && !tok_is(TOK_NEWLINE)) {

        /* Check for redirections */
        if (tok_is(TOK_REDIR_OUT) || tok_is(TOK_REDIR_APP) ||
            tok_is(TOK_REDIR_IN) || tok_is(TOK_REDIR_ERR)) {
            sh_token_t *op = tok_next();
            if (!tok_is(TOK_WORD)) break;
            sh_token_t *fname = tok_next();

            if (node->cmd.redir_count < SH_MAX_REDIR) {
                sh_redir_t *r = &node->cmd.redirs[node->cmd.redir_count++];
                strcpy(r->filename, fname->value);
                if (op->type == TOK_REDIR_OUT) {
                    r->target_fd = 1;
                    r->mode = 0; /* write */
                } else if (op->type == TOK_REDIR_APP) {
                    r->target_fd = 1;
                    r->mode = 1; /* append */
                } else if (op->type == TOK_REDIR_IN) {
                    r->target_fd = 0;
                    r->mode = 2; /* read */
                } else if (op->type == TOK_REDIR_ERR) {
                    r->target_fd = 2;
                    r->mode = 0; /* write */
                }
            }
            continue;
        }

        if (tok_is(TOK_WORD)) {
            sh_token_t *w = tok_next();
            if (node->cmd.argc < SH_MAX_ARGS) {
                strncpy(node->cmd.argv[node->cmd.argc], w->value, 255);
                node->cmd.argc++;
            }
            continue;
        }

        break;
    }

    if (node->cmd.argc == 0 && node->cmd.redir_count == 0)
        return NULL;

    return node;
}

/* Parse a compound command (if/for/while/case) or a simple command */
static sh_node_t *parse_command(void) {
    if (tok_is_word("if"))    return parse_if();
    if (tok_is_word("for"))   return parse_for();
    if (tok_is_word("while")) return parse_while(0);
    if (tok_is_word("until")) return parse_while(1);
    if (tok_is_word("case"))  return parse_case();

    if (tok_is(TOK_LPAREN)) {
        tok_next(); /* skip ( */
        sh_node_t *inner = parse_list();
        if (tok_is(TOK_RPAREN)) tok_next();
        return inner;
    }

    return parse_simple_command();
}

/* Parse a pipeline: cmd1 | cmd2 | cmd3 */
static sh_node_t *parse_pipeline(void) {
    sh_node_t *first = parse_command();
    if (!first) return NULL;

    if (!tok_is(TOK_PIPE))
        return first;

    sh_node_t *node = sh_alloc_node();
    if (!node) return first;
    node->type = SH_PIPE;
    node->pipeline.stages[0] = first;
    node->pipeline.count = 1;

    while (tok_is(TOK_PIPE) && node->pipeline.count < SH_MAX_PIPE) {
        tok_next(); /* skip | */
        sh_node_t *stage = parse_command();
        if (!stage) break;
        node->pipeline.stages[node->pipeline.count++] = stage;
    }

    return node;
}

/* Parse a list: pipeline && pipeline || pipeline ; pipeline & pipeline */
static sh_node_t *parse_list(void) {
    sh_node_t *first = parse_pipeline();
    if (!first) return NULL;

    /* Skip optional semicolons/newlines at end */
    int has_operator = tok_is(TOK_AND) || tok_is(TOK_OR) ||
                       tok_is(TOK_SEMI) || tok_is(TOK_BG);

    if (!has_operator) {
        return first;
    }

    sh_node_t *node = sh_alloc_node();
    if (!node) return first;
    node->type = SH_LIST;
    node->list.nodes[0] = first;
    node->list.count = 1;

    while ((tok_is(TOK_AND) || tok_is(TOK_OR) || tok_is(TOK_SEMI) || tok_is(TOK_BG)) &&
           node->list.count < SH_MAX_TOKENS / 2 - 1) {
        sh_token_t *op = tok_next();
        int op_type = 0; /* ; */
        if (op->type == TOK_AND)  op_type = 1; /* && */
        if (op->type == TOK_OR)   op_type = 2; /* || */
        if (op->type == TOK_BG)   op_type = 3; /* & */
        node->list.operators[node->list.count - 1] = op_type;

        /* After ; or & at EOF is valid */
        if (tok_is(TOK_EOF) || tok_is(TOK_RPAREN))
            break;
        /* After ; skip if next is a keyword ending a block */
        if (tok_is_word("fi") || tok_is_word("done") || tok_is_word("esac") ||
            tok_is_word("then") || tok_is_word("else") || tok_is_word("elif") ||
            tok_is_word("do"))
            break;

        sh_node_t *next = parse_pipeline();
        if (!next) break;
        node->list.nodes[node->list.count] = next;
        node->list.count++;
    }

    return node;
}

/* parse_body_until removed — parse_list() handles stop-words via tok_is_word() */

/* Parse if/then/elif/else/fi */
static sh_node_t *parse_if(void) {
    tok_next(); /* skip 'if' */
    sh_node_t *node = sh_alloc_node();
    if (!node) return NULL;
    node->type = SH_IF;

    /* Parse condition (up to 'then') */
    node->if_node.condition = parse_list();

    /* Expect 'then' */
    if (tok_is(TOK_SEMI)) tok_next();
    if (tok_is_word("then")) tok_next();

    /* Parse then-body */
    node->if_node.then_body = parse_list();

    /* Check for elif */
    if (tok_is(TOK_SEMI)) tok_next();
    if (tok_is_word("elif")) {
        /* Recursive: elif becomes a nested if */
        node->if_node.else_body = parse_if();
    } else if (tok_is_word("else")) {
        tok_next(); /* skip 'else' */
        if (tok_is(TOK_SEMI)) tok_next();
        node->if_node.else_body = parse_list();
    }

    /* Expect 'fi' */
    if (tok_is(TOK_SEMI)) tok_next();
    if (tok_is_word("fi")) tok_next();

    return node;
}

/* Parse for VAR in WORDS; do BODY; done */
static sh_node_t *parse_for(void) {
    tok_next(); /* skip 'for' */
    sh_node_t *node = sh_alloc_node();
    if (!node) return NULL;
    node->type = SH_FOR;

    /* Get variable name */
    if (tok_is(TOK_WORD)) {
        strncpy(node->for_node.var, tok_next()->value, 63);
    }

    /* Expect 'in' */
    if (tok_is_word("in")) {
        tok_next();
        /* Collect words until 'do' or ';' */
        while (!tok_is(TOK_EOF) && !tok_is_word("do") && !tok_is(TOK_SEMI) &&
               node->for_node.word_count < SH_MAX_ARGS) {
            if (tok_is(TOK_WORD)) {
                strncpy(node->for_node.words[node->for_node.word_count],
                        tok_next()->value, 255);
                node->for_node.word_count++;
            } else {
                break;
            }
        }
    }

    /* Expect 'do' */
    if (tok_is(TOK_SEMI)) tok_next();
    if (tok_is_word("do")) tok_next();

    /* Parse body */
    node->for_node.body = parse_list();

    /* Expect 'done' */
    if (tok_is(TOK_SEMI)) tok_next();
    if (tok_is_word("done")) tok_next();

    return node;
}

/* Parse while/until COND; do BODY; done */
static sh_node_t *parse_while(int is_until) {
    tok_next(); /* skip 'while' or 'until' */
    sh_node_t *node = sh_alloc_node();
    if (!node) return NULL;
    node->type = SH_WHILE;
    node->while_node.is_until = is_until;

    /* Parse condition */
    node->while_node.condition = parse_list();

    /* Expect 'do' */
    if (tok_is(TOK_SEMI)) tok_next();
    if (tok_is_word("do")) tok_next();

    /* Parse body */
    node->while_node.body = parse_list();

    /* Expect 'done' */
    if (tok_is(TOK_SEMI)) tok_next();
    if (tok_is_word("done")) tok_next();

    return node;
}

/* Parse case WORD in pattern) body ;; ... esac */
static sh_node_t *parse_case(void) {
    tok_next(); /* skip 'case' */
    sh_node_t *node = sh_alloc_node();
    if (!node) return NULL;
    node->type = SH_CASE;

    /* Get the word to match */
    if (tok_is(TOK_WORD)) {
        strncpy(node->case_node.word, tok_next()->value, 255);
    }

    /* Expect 'in' */
    if (tok_is_word("in")) tok_next();
    if (tok_is(TOK_SEMI)) tok_next();

    /* Parse cases until 'esac' */
    while (!tok_is(TOK_EOF) && !tok_is_word("esac") &&
           node->case_node.case_count < SH_MAX_CASES) {
        /* Pattern word followed by ) */
        if (tok_is(TOK_WORD)) {
            int idx = node->case_node.case_count;
            strncpy(node->case_node.cases[idx].pattern, tok_next()->value, 255);

            /* Skip ) — it's part of the WORD token in our simple parser */
            if (tok_is(TOK_RPAREN)) tok_next();

            /* Parse body until ;; or esac */
            node->case_node.cases[idx].body = parse_list();
            node->case_node.case_count++;

            /* ;; is tokenized as two TOK_SEMI */
            if (tok_is(TOK_SEMI)) tok_next();
            if (tok_is(TOK_SEMI)) tok_next();
        } else {
            break;
        }
    }

    if (tok_is_word("esac")) tok_next();

    return node;
}

sh_node_t *sh_parse(sh_token_t *tokens, int token_count) {
    sh_reset_pool();
    tok_stream = tokens;
    tok_pos = 0;
    tok_total = token_count;

    return parse_list();
}

/* ═══ Executor ═════════════════════════════════════════════════ */

/* External shell command dispatch — defined in shell.c */
extern int shell_dispatch_command(int argc, char *argv[]);

/* Execute a simple command node */
static int exec_simple_cmd(sh_cmd_t *cmd) {
    if (cmd->argc == 0)
        return 0;

    /* Check for variable assignment: FOO=bar */
    if (cmd->argc == 1) {
        char *eq = strchr(cmd->argv[0], '=');
        if (eq && eq != cmd->argv[0]) {
            /* It's an assignment */
            char name[64];
            int nlen = eq - cmd->argv[0];
            if (nlen > 63) nlen = 63;
            memcpy(name, cmd->argv[0], nlen);
            name[nlen] = '\0';
            env_set(name, eq + 1);
            return 0;
        }
    }

    /* Also check multi-word: FOO=bar cmd args — set FOO for this command */
    /* (simplified: we just handle standalone assignments) */

    /* Expand variables in each argument */
    char *expanded_argv[SH_MAX_ARGS];
    static char expanded_bufs[SH_MAX_ARGS][256];
    for (int i = 0; i < cmd->argc; i++) {
        sh_expand(cmd->argv[i], expanded_bufs[i], 256);
        expanded_argv[i] = expanded_bufs[i];
    }

    /* Glob expansion */
    static char glob_bufs[SH_MAX_ARGS * 4][256];
    char *final_argv[SH_MAX_ARGS * 4];
    int final_argc = 0;

    for (int i = 0; i < cmd->argc && final_argc < SH_MAX_ARGS * 4 - 1; i++) {
        /* Check if word contains glob chars */
        int has_glob = 0;
        for (const char *c = expanded_argv[i]; *c; c++) {
            if (*c == '*' || *c == '?' || *c == '[') {
                has_glob = 1;
                break;
            }
        }
        if (has_glob) {
            char matches[32][256];
            int nmatches = glob_expand(expanded_argv[i], matches, 32);
            if (nmatches > 0) {
                for (int j = 0; j < nmatches && final_argc < SH_MAX_ARGS * 4 - 1; j++) {
                    strncpy(glob_bufs[final_argc], matches[j], 255);
                    glob_bufs[final_argc][255] = '\0';
                    final_argv[final_argc] = glob_bufs[final_argc];
                    final_argc++;
                }
                continue;
            }
            /* No matches: pass pattern literally */
        }
        strncpy(glob_bufs[final_argc], expanded_argv[i], 255);
        glob_bufs[final_argc][255] = '\0';
        final_argv[final_argc] = glob_bufs[final_argc];
        final_argc++;
    }

    /* Handle redirections */
    int saved_stdout = -1;
    int redir_fds[SH_MAX_REDIR];
    int redir_count = 0;
    int tid = task_get_current();

    for (int i = 0; i < cmd->redir_count; i++) {
        sh_redir_t *r = &cmd->redirs[i];

        /* Expand the filename */
        char fname[256];
        sh_expand(r->filename, fname, sizeof(fname));

        if (r->mode == 2) {
            /* Input redirection: < */
            uint32_t parent;
            char name[28];
            int ino = fs_resolve_path(fname, &parent, name);
            if (ino < 0) {
                printf("sh: %s: No such file\n", fname);
                sh_last_exit = 1;
                goto redir_cleanup;
            }
            /* Save old stdin pipe mode and redirect */
            /* For builtins, we use a pipe: write file content, command reads */
            /* Simplified: we don't have real stdin for builtins yet */
            /* This will work properly once pipeline execution uses tasks */
        } else {
            /* Output redirection: > or >> */
            /* Save current stdout capture state */
            if (r->target_fd == 1 && saved_stdout < 0) {
                saved_stdout = 0; /* mark as redirected */
            }
            /* Create file if needed */
            if (r->mode == 0) {
                /* > (truncate) */
                fs_create_file(fname, 0);
                uint32_t parent;
                char name[28];
                int ino = fs_resolve_path(fname, &parent, name);
                if (ino >= 0) {
                    inode_t node;
                    fs_read_inode(ino, &node);
                    if (node.size > 0)
                        fs_truncate_inode(ino, 0);
                }
            } else {
                /* >> (append) - create if not exists */
                fs_create_file(fname, 0);
            }
        }
    }

    /* If we have output redirections, capture with pipe mode */
    extern int shell_pipe_mode;
    extern int shell_pipe_len;
    extern char shell_pipe_buf[];

    int old_pipe_mode = shell_pipe_mode;
    int old_pipe_len = shell_pipe_len;

    if (cmd->redir_count > 0 && saved_stdout >= 0) {
        shell_pipe_mode = 1;
        shell_pipe_len = 0;
    }

    /* Dispatch the command */
    int rc = shell_dispatch_command(final_argc, final_argv);

    /* Write captured output to redirect files */
    if (cmd->redir_count > 0 && saved_stdout >= 0) {
        shell_pipe_mode = old_pipe_mode;
        shell_pipe_buf[shell_pipe_len] = '\0';

        for (int i = 0; i < cmd->redir_count; i++) {
            sh_redir_t *r = &cmd->redirs[i];
            if (r->target_fd != 1 && r->target_fd != 2)
                continue;
            if (r->mode == 2)
                continue; /* skip input redir */

            char fname[256];
            sh_expand(r->filename, fname, sizeof(fname));

            uint32_t parent;
            char name[28];
            int ino = fs_resolve_path(fname, &parent, name);
            if (ino >= 0 && shell_pipe_len > 0) {
                uint32_t offset = 0;
                if (r->mode == 1) {
                    /* Append: get current size */
                    inode_t node;
                    fs_read_inode(ino, &node);
                    offset = node.size;
                }
                fs_write_at(ino, (const uint8_t *)shell_pipe_buf,
                            offset, shell_pipe_len);
            }
        }
        shell_pipe_len = old_pipe_len;
    }

    sh_last_exit = rc;

redir_cleanup:
    for (int i = 0; i < redir_count; i++) {
        if (redir_fds[i] >= 0)
            pipe_close(redir_fds[i], tid);
    }

    return sh_last_exit;
}

/* Execute a pipeline: cmd1 | cmd2 | cmd3
 * Uses shell_pipe_buf for capture between stages (builtin-to-builtin).
 * For real processes (ELF), kernel pipes would be used. */
static int exec_pipeline(sh_pipeline_t *pipeline) {
    if (pipeline->count == 0)
        return 0;
    if (pipeline->count == 1)
        return sh_execute(pipeline->stages[0]);

    extern int shell_pipe_mode;
    extern int shell_pipe_len;
    extern char shell_pipe_buf[];
    #define SHELL_PIPE_BUF_SIZE 4096

    /* For each stage: capture output, feed as "stdin" to next */
    /* Since builtins use printf (not real fds), we use shell_pipe_buf */

    /* We maintain two buffers: current input and current output */
    static char input_buf[SHELL_PIPE_BUF_SIZE];
    int input_len = 0;

    for (int i = 0; i < pipeline->count; i++) {
        int is_last = (i == pipeline->count - 1);

        if (!is_last) {
            /* Capture output */
            shell_pipe_mode = 1;
            shell_pipe_len = 0;
        }

        /* If we have input from previous stage, set up for reading */
        /* TODO: For now, piped input is available via shell_get_pipe_input() */
        extern void shell_set_pipe_input(const char *buf, int len);
        if (i > 0)
            shell_set_pipe_input(input_buf, input_len);
        else
            shell_set_pipe_input(NULL, 0);

        sh_execute(pipeline->stages[i]);

        if (!is_last) {
            shell_pipe_mode = 0;
            shell_pipe_buf[shell_pipe_len] = '\0';
            /* Copy output to input buffer for next stage */
            memcpy(input_buf, shell_pipe_buf, shell_pipe_len);
            input_len = shell_pipe_len;
        } else {
            shell_set_pipe_input(NULL, 0);
        }
    }

    return sh_last_exit;
}

/* Execute a list: cmd1 && cmd2 || cmd3 ; cmd4 */
static int exec_list(sh_list_t *list) {
    if (list->count == 0) return 0;

    int rc = sh_execute(list->nodes[0]);

    for (int i = 1; i < list->count; i++) {
        int op = list->operators[i - 1];
        switch (op) {
        case 0: /* ; — always execute */
            rc = sh_execute(list->nodes[i]);
            break;
        case 1: /* && — only if previous succeeded */
            if (rc == 0)
                rc = sh_execute(list->nodes[i]);
            break;
        case 2: /* || — only if previous failed */
            if (rc != 0)
                rc = sh_execute(list->nodes[i]);
            break;
        case 3: /* & — run in background */
            /* For now, just run synchronously (proper bg needs task_create) */
            rc = sh_execute(list->nodes[i]);
            break;
        }
    }
    return rc;
}

/* Execute if/elif/else/fi */
static int exec_if(sh_if_t *if_node) {
    int cond = sh_execute(if_node->condition);
    if (cond == 0) {
        return if_node->then_body ? sh_execute(if_node->then_body) : 0;
    } else {
        return if_node->else_body ? sh_execute(if_node->else_body) : 0;
    }
}

/* Execute for loop */
static int exec_for(sh_for_t *for_node) {
    int rc = 0;
    for (int i = 0; i < for_node->word_count; i++) {
        /* Expand the word */
        char expanded[256];
        sh_expand(for_node->words[i], expanded, sizeof(expanded));

        /* Check for glob in the word */
        int has_glob = 0;
        for (const char *c = expanded; *c; c++) {
            if (*c == '*' || *c == '?' || *c == '[') {
                has_glob = 1;
                break;
            }
        }

        if (has_glob) {
            char matches[32][256];
            int nmatches = glob_expand(expanded, matches, 32);
            for (int j = 0; j < nmatches; j++) {
                env_set(for_node->var, matches[j]);
                if (for_node->body)
                    rc = sh_execute(for_node->body);
            }
        } else {
            env_set(for_node->var, expanded);
            if (for_node->body)
                rc = sh_execute(for_node->body);
        }
    }
    return rc;
}

/* Execute while/until loop */
static int exec_while(sh_while_t *while_node) {
    int rc = 0;
    int max_iter = 10000; /* prevent infinite loops */
    while (max_iter-- > 0) {
        int cond = sh_execute(while_node->condition);
        int should_run = while_node->is_until ? (cond != 0) : (cond == 0);
        if (!should_run)
            break;
        if (while_node->body)
            rc = sh_execute(while_node->body);
    }
    return rc;
}

/* Execute case statement */
static int exec_case(sh_case_t *case_node) {
    /* Expand the word */
    char word[256];
    sh_expand(case_node->word, word, sizeof(word));

    for (int i = 0; i < case_node->case_count; i++) {
        char pattern[256];
        sh_expand(case_node->cases[i].pattern, pattern, sizeof(pattern));

        /* Simple pattern matching: * matches anything, otherwise exact */
        int match = 0;
        if (strcmp(pattern, "*") == 0) {
            match = 1;
        } else {
            /* Use glob-style matching */
            extern int glob_match(const char *pattern, const char *str);
            match = glob_match(pattern, word);
        }

        if (match) {
            if (case_node->cases[i].body)
                return sh_execute(case_node->cases[i].body);
            return 0;
        }
    }
    return 0;
}

/* Main AST executor */
int sh_execute(sh_node_t *node) {
    if (!node) return 0;

    switch (node->type) {
    case SH_CMD:
        return exec_simple_cmd(&node->cmd);
    case SH_PIPE:
        return exec_pipeline(&node->pipeline);
    case SH_LIST:
        return exec_list(&node->list);
    case SH_IF:
        return exec_if(&node->if_node);
    case SH_FOR:
        return exec_for(&node->for_node);
    case SH_WHILE:
        return exec_while(&node->while_node);
    case SH_CASE:
        return exec_case(&node->case_node);
    case SH_SUBSHELL:
        return 0; /* handled via SH_LIST inside */
    }
    return 1;
}

/* ═══ Top-level entry point ════════════════════════════════════ */

int sh_run(sh_token_t *tokens, int token_count) {
    if (token_count <= 0)
        return 0;

    sh_node_t *ast = sh_parse(tokens, token_count);
    if (!ast)
        return 1;

    return sh_execute(ast);
}
