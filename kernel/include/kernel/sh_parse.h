#ifndef _KERNEL_SH_PARSE_H
#define _KERNEL_SH_PARSE_H

#include <stddef.h>

/* ── Token types ── */
typedef enum {
    TOK_WORD,       /* regular word/argument */
    TOK_PIPE,       /* | */
    TOK_REDIR_OUT,  /* > */
    TOK_REDIR_APP,  /* >> */
    TOK_REDIR_IN,   /* < */
    TOK_REDIR_ERR,  /* 2> */
    TOK_AND,        /* && */
    TOK_OR,         /* || */
    TOK_BG,         /* & */
    TOK_SEMI,       /* ; */
    TOK_LPAREN,     /* ( */
    TOK_RPAREN,     /* ) */
    TOK_NEWLINE,
    TOK_EOF,
} sh_token_type_t;

typedef struct {
    sh_token_type_t type;
    char value[256];
} sh_token_t;

#define SH_MAX_TOKENS 128

/* ── AST node types (control flow) ── */
typedef enum {
    SH_CMD,         /* simple command */
    SH_PIPE,        /* pipeline: cmd1 | cmd2 */
    SH_IF,          /* if/then/elif/else/fi */
    SH_FOR,         /* for VAR in WORDS; do ... done */
    SH_WHILE,       /* while/until ... do ... done */
    SH_CASE,        /* case WORD in pattern) ... esac */
    SH_LIST,        /* command list: A && B || C ; D */
    SH_SUBSHELL,    /* ( ... ) */
} sh_node_type_t;

/* Redirection descriptor */
typedef struct {
    int target_fd;   /* 0=stdin, 1=stdout, 2=stderr */
    int mode;        /* 0=write, 1=append, 2=read */
    char filename[256];
} sh_redir_t;

#define SH_MAX_ARGS   64
#define SH_MAX_REDIR  8
#define SH_MAX_PIPE   8
#define SH_MAX_CASES  16

/* Forward declaration */
struct sh_node;

/* Simple command: argv[] + redirections */
typedef struct {
    int argc;
    char argv[SH_MAX_ARGS][256];
    int redir_count;
    sh_redir_t redirs[SH_MAX_REDIR];
    int background;     /* trailing & */
} sh_cmd_t;

/* Pipeline: cmd1 | cmd2 | cmd3 */
typedef struct {
    int count;
    struct sh_node *stages[SH_MAX_PIPE];
} sh_pipeline_t;

/* List: A && B || C ; D */
typedef struct {
    int count;
    struct sh_node *nodes[SH_MAX_TOKENS / 2];
    int operators[SH_MAX_TOKENS / 2]; /* 0=;  1=&&  2=||  3=& */
} sh_list_t;

/* if/elif/else/fi */
typedef struct {
    struct sh_node *condition;
    struct sh_node *then_body;
    struct sh_node *elif_cond;   /* NULL if no elif */
    struct sh_node *elif_body;   /* NULL if no elif */
    struct sh_node *else_body;   /* NULL if no else */
} sh_if_t;

/* for VAR in WORDS; do BODY; done */
typedef struct {
    char var[64];
    int word_count;
    char words[SH_MAX_ARGS][256];
    struct sh_node *body;
} sh_for_t;

/* while/until COND; do BODY; done */
typedef struct {
    int is_until;    /* 1 for until, 0 for while */
    struct sh_node *condition;
    struct sh_node *body;
} sh_while_t;

/* case WORD in pattern) body ;; ... esac */
typedef struct {
    char word[256];
    int case_count;
    struct {
        char pattern[256];
        struct sh_node *body;
    } cases[SH_MAX_CASES];
} sh_case_t;

/* AST node */
typedef struct sh_node {
    sh_node_type_t type;
    union {
        sh_cmd_t      cmd;
        sh_pipeline_t pipeline;
        sh_list_t     list;
        sh_if_t       if_node;
        sh_for_t      for_node;
        sh_while_t    while_node;
        sh_case_t     case_node;
    };
    /* subshell uses pipeline or list inside */
} sh_node_t;

/* ── Public API ── */

/* Tokenize a command string into an array of tokens.
 * Returns number of tokens, or -1 on error. */
int sh_tokenize(const char *input, sh_token_t *tokens, int max_tokens);

/* Expand variables in a single token value.
 * Handles $VAR, ${VAR}, $?, $$, $0, ~ expansion.
 * Returns 0 on success. */
int sh_expand(const char *input, char *output, size_t output_size);

/* Parse tokens into an AST. Returns root node (statically allocated)
 * or NULL on parse error. */
sh_node_t *sh_parse(sh_token_t *tokens, int token_count);

/* Execute an AST node. Returns exit code. */
int sh_execute(sh_node_t *node);

/* Execute a token array as a simple command or pipeline.
 * This is the main entry point from shell_process_command(). */
int sh_run(sh_token_t *tokens, int token_count);

/* Get/set last exit code */
int sh_get_exit_code(void);
void sh_set_exit_code(int code);

/* Shell integration — defined in shell.c */
int shell_dispatch_command(int argc, char *argv[]);
int shell_job_add(int pid, int tid, const char *cmd);
void shell_set_pipe_input(const char *buf, int len);
int shell_read_pipe_input(char *buf, int count);
int shell_has_pipe_input(void);

#endif
