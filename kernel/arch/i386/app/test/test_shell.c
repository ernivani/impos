#include <kernel/test.h>
#include <kernel/sh_parse.h>
#include <kernel/glob.h>
#include <kernel/shell.h>
#include <kernel/env.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void test_phase12_tokenizer(void) {
    printf("== Phase 12 Tokenizer Tests ==\n");

    sh_token_t tokens[SH_MAX_TOKENS];
    int n;

    /* Basic word tokenization */
    n = sh_tokenize("echo hello world", tokens, SH_MAX_TOKENS);
    TEST_ASSERT(n == 3, "p12: tokenize 3 words");
    TEST_ASSERT(tokens[0].type == TOK_WORD, "p12: first token is WORD");
    TEST_ASSERT(strcmp(tokens[0].value, "echo") == 0, "p12: first word is 'echo'");
    TEST_ASSERT(strcmp(tokens[1].value, "hello") == 0, "p12: second word is 'hello'");
    TEST_ASSERT(strcmp(tokens[2].value, "world") == 0, "p12: third word is 'world'");

    /* Pipe operator */
    n = sh_tokenize("ls | grep foo", tokens, SH_MAX_TOKENS);
    TEST_ASSERT(n == 4, "p12: tokenize pipe command");
    TEST_ASSERT(tokens[0].type == TOK_WORD, "p12: ls is WORD");
    TEST_ASSERT(tokens[1].type == TOK_PIPE, "p12: | is PIPE");
    TEST_ASSERT(tokens[2].type == TOK_WORD, "p12: grep is WORD");
    TEST_ASSERT(tokens[3].type == TOK_WORD, "p12: foo is WORD");

    /* Multiple pipes */
    n = sh_tokenize("a | b | c", tokens, SH_MAX_TOKENS);
    TEST_ASSERT(n == 5, "p12: tokenize multi-pipe");
    TEST_ASSERT(tokens[1].type == TOK_PIPE, "p12: first pipe");
    TEST_ASSERT(tokens[3].type == TOK_PIPE, "p12: second pipe");

    /* Redirect operators */
    n = sh_tokenize("echo hi > out.txt", tokens, SH_MAX_TOKENS);
    TEST_ASSERT(n == 4, "p12: tokenize redirect >");
    TEST_ASSERT(tokens[2].type == TOK_REDIR_OUT, "p12: > is REDIR_OUT");
    TEST_ASSERT(strcmp(tokens[3].value, "out.txt") == 0, "p12: redirect filename");

    n = sh_tokenize("echo hi >> out.txt", tokens, SH_MAX_TOKENS);
    TEST_ASSERT(tokens[2].type == TOK_REDIR_APP, "p12: >> is REDIR_APP");

    n = sh_tokenize("cat < in.txt", tokens, SH_MAX_TOKENS);
    TEST_ASSERT(tokens[1].type == TOK_REDIR_IN, "p12: < is REDIR_IN");

    /* && and || operators */
    n = sh_tokenize("cmd1 && cmd2 || cmd3", tokens, SH_MAX_TOKENS);
    TEST_ASSERT(n == 5, "p12: tokenize &&/||");
    TEST_ASSERT(tokens[1].type == TOK_AND, "p12: && is AND");
    TEST_ASSERT(tokens[3].type == TOK_OR, "p12: || is OR");

    /* Background and semicolons */
    n = sh_tokenize("sleep 5 &", tokens, SH_MAX_TOKENS);
    TEST_ASSERT(tokens[2].type == TOK_BG, "p12: & is BG");

    n = sh_tokenize("cmd1 ; cmd2", tokens, SH_MAX_TOKENS);
    TEST_ASSERT(tokens[1].type == TOK_SEMI, "p12: ; is SEMI");

    /* Single quotes — literal, no expansion */
    n = sh_tokenize("echo 'hello world'", tokens, SH_MAX_TOKENS);
    TEST_ASSERT(n == 2, "p12: single-quoted string is one token");
    TEST_ASSERT(strcmp(tokens[1].value, "hello world") == 0, "p12: single-quote content");

    /* Double quotes — preserves spaces */
    n = sh_tokenize("echo \"hello world\"", tokens, SH_MAX_TOKENS);
    TEST_ASSERT(n == 2, "p12: double-quoted string is one token");
    TEST_ASSERT(strcmp(tokens[1].value, "hello world") == 0, "p12: double-quote content");

    /* Backslash escaping */
    n = sh_tokenize("echo hello\\ world", tokens, SH_MAX_TOKENS);
    TEST_ASSERT(n == 2, "p12: backslash-escaped space");
    TEST_ASSERT(strcmp(tokens[1].value, "hello world") == 0, "p12: escaped space content");

    /* Empty input */
    n = sh_tokenize("", tokens, SH_MAX_TOKENS);
    TEST_ASSERT(n == 0, "p12: empty input gives 0 tokens");

    /* Only spaces */
    n = sh_tokenize("   ", tokens, SH_MAX_TOKENS);
    TEST_ASSERT(n == 0, "p12: whitespace-only gives 0 tokens");

    /* Comment */
    n = sh_tokenize("echo hi # comment", tokens, SH_MAX_TOKENS);
    TEST_ASSERT(n == 2, "p12: comment strips remainder");

    /* Parentheses */
    n = sh_tokenize("(echo hi)", tokens, SH_MAX_TOKENS);
    TEST_ASSERT(tokens[0].type == TOK_LPAREN, "p12: ( is LPAREN");
    TEST_ASSERT(tokens[2].type == TOK_RPAREN, "p12: ) is RPAREN");

    /* EOF sentinel */
    n = sh_tokenize("hello", tokens, SH_MAX_TOKENS);
    TEST_ASSERT(tokens[n].type == TOK_EOF, "p12: tokens end with EOF");

    /* 2> stderr redirect */
    n = sh_tokenize("cmd 2> err.log", tokens, SH_MAX_TOKENS);
    TEST_ASSERT(n == 3, "p12: tokenize 2> redirect");
    TEST_ASSERT(tokens[1].type == TOK_REDIR_ERR, "p12: 2> is REDIR_ERR");
}

static void test_phase12_glob(void) {
    printf("== Phase 12 Glob Tests ==\n");

    /* glob_match basic patterns */
    TEST_ASSERT(glob_match("*", "anything") == 1, "p12: * matches anything");
    TEST_ASSERT(glob_match("*", "") == 1, "p12: * matches empty");
    TEST_ASSERT(glob_match("hello", "hello") == 1, "p12: exact match");
    TEST_ASSERT(glob_match("hello", "world") == 0, "p12: no match");

    /* Wildcard * in middle */
    TEST_ASSERT(glob_match("he*llo", "hello") == 1, "p12: he*llo matches hello");
    TEST_ASSERT(glob_match("he*llo", "heccccllo") == 1, "p12: * in middle matches multi");
    TEST_ASSERT(glob_match("*.c", "foo.c") == 1, "p12: *.c matches foo.c");
    TEST_ASSERT(glob_match("*.c", "foo.h") == 0, "p12: *.c rejects foo.h");
    TEST_ASSERT(glob_match("*.c", ".c") == 1, "p12: *.c matches .c");

    /* Question mark ? */
    TEST_ASSERT(glob_match("?", "a") == 1, "p12: ? matches single char");
    TEST_ASSERT(glob_match("?", "") == 0, "p12: ? rejects empty");
    TEST_ASSERT(glob_match("?.c", "a.c") == 1, "p12: ?.c matches a.c");
    TEST_ASSERT(glob_match("?.c", "ab.c") == 0, "p12: ?.c rejects ab.c");

    /* Character classes [abc] */
    TEST_ASSERT(glob_match("[abc]", "a") == 1, "p12: [abc] matches a");
    TEST_ASSERT(glob_match("[abc]", "b") == 1, "p12: [abc] matches b");
    TEST_ASSERT(glob_match("[abc]", "d") == 0, "p12: [abc] rejects d");

    /* Character ranges [a-z] */
    TEST_ASSERT(glob_match("[a-z]", "m") == 1, "p12: [a-z] matches m");
    TEST_ASSERT(glob_match("[a-z]", "A") == 0, "p12: [a-z] rejects A");
    TEST_ASSERT(glob_match("[0-9]", "5") == 1, "p12: [0-9] matches 5");

    /* Negated character class */
    TEST_ASSERT(glob_match("[!abc]", "d") == 1, "p12: [!abc] matches d");
    TEST_ASSERT(glob_match("[!abc]", "a") == 0, "p12: [!abc] rejects a");

    /* Complex patterns */
    TEST_ASSERT(glob_match("test_*.c", "test_foo.c") == 1, "p12: test_*.c matches");
    TEST_ASSERT(glob_match("test_*.c", "main.c") == 0, "p12: test_*.c rejects main.c");
    TEST_ASSERT(glob_match("*.*", "file.txt") == 1, "p12: *.* matches file.txt");
    TEST_ASSERT(glob_match("*.*", "noext") == 0, "p12: *.* rejects noext");
}

static void test_phase12_expand(void) {
    printf("== Phase 12 Variable Expansion Tests ==\n");

    char out[256];

    /* Basic $VAR */
    env_set("TESTVAR", "hello");
    sh_expand("$TESTVAR", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "hello") == 0, "p12: $TESTVAR expands to hello");

    /* ${VAR} braces */
    sh_expand("${TESTVAR}", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "hello") == 0, "p12: ${TESTVAR} expands to hello");

    /* $VAR in context */
    sh_expand("say $TESTVAR!", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "say hello!") == 0, "p12: $VAR in context");

    /* Undefined variable */
    sh_expand("$UNDEFINED_VAR_XYZ", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "") == 0, "p12: undefined var expands to empty");

    /* $? exit code */
    sh_set_exit_code(42);
    sh_expand("$?", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "42") == 0, "p12: $? expands to exit code");
    sh_set_exit_code(0);

    /* $0 shell name */
    sh_expand("$0", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "sh") == 0, "p12: $0 expands to sh");

    /* ~ expansion */
    const char *home = env_get("HOME");
    sh_expand("~", out, sizeof(out));
    TEST_ASSERT(home && strcmp(out, home) == 0, "p12: ~ expands to $HOME");

    sh_expand("~/subdir", out, sizeof(out));
    char expected[256];
    snprintf(expected, sizeof(expected), "%s/subdir", home ? home : "");
    TEST_ASSERT(strcmp(out, expected) == 0, "p12: ~/subdir expands correctly");

    /* No expansion for literal text */
    sh_expand("hello world", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "hello world") == 0, "p12: literal text unchanged");

    /* $$ pid */
    sh_expand("$$", out, sizeof(out));
    TEST_ASSERT(strlen(out) > 0, "p12: $$ expands to non-empty string");
    /* PID should be a number */
    TEST_ASSERT(out[0] >= '0' && out[0] <= '9', "p12: $$ is numeric");

    /* Multiple vars */
    env_set("A", "foo");
    env_set("B", "bar");
    sh_expand("$A-$B", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "foo-bar") == 0, "p12: multiple vars expand");

    /* Cleanup */
    env_unset("TESTVAR");
    env_unset("A");
    env_unset("B");
}

static void test_phase12_parser(void) {
    printf("== Phase 12 Parser Tests ==\n");

    sh_token_t tokens[SH_MAX_TOKENS];
    int n;

    /* Parse simple command */
    n = sh_tokenize("echo hello", tokens, SH_MAX_TOKENS);
    sh_node_t *ast = sh_parse(tokens, n);
    TEST_ASSERT(ast != NULL, "p12: parse simple command");
    TEST_ASSERT(ast->type == SH_CMD, "p12: simple cmd is SH_CMD");
    TEST_ASSERT(ast->cmd.argc == 2, "p12: simple cmd has 2 args");
    TEST_ASSERT(strcmp(ast->cmd.argv[0], "echo") == 0, "p12: cmd argv[0] is echo");

    /* Parse pipeline */
    n = sh_tokenize("ls | grep foo | wc", tokens, SH_MAX_TOKENS);
    ast = sh_parse(tokens, n);
    TEST_ASSERT(ast != NULL, "p12: parse pipeline");
    TEST_ASSERT(ast->type == SH_PIPE, "p12: pipeline is SH_PIPE");
    TEST_ASSERT(ast->pipeline.count == 3, "p12: pipeline has 3 stages");

    /* Parse && chain */
    n = sh_tokenize("true && echo ok", tokens, SH_MAX_TOKENS);
    ast = sh_parse(tokens, n);
    TEST_ASSERT(ast != NULL, "p12: parse && chain");
    TEST_ASSERT(ast->type == SH_LIST, "p12: && chain is SH_LIST");
    TEST_ASSERT(ast->list.count == 2, "p12: && list has 2 nodes");
    TEST_ASSERT(ast->list.operators[0] == 1, "p12: operator is && (1)");

    /* Parse || chain */
    n = sh_tokenize("false || echo fallback", tokens, SH_MAX_TOKENS);
    ast = sh_parse(tokens, n);
    TEST_ASSERT(ast != NULL, "p12: parse || chain");
    TEST_ASSERT(ast->type == SH_LIST, "p12: || chain is SH_LIST");
    TEST_ASSERT(ast->list.operators[0] == 2, "p12: operator is || (2)");

    /* Parse with redirect */
    n = sh_tokenize("echo hi > /tmp/out", tokens, SH_MAX_TOKENS);
    ast = sh_parse(tokens, n);
    TEST_ASSERT(ast != NULL, "p12: parse redirect cmd");
    TEST_ASSERT(ast->type == SH_CMD, "p12: redirect is SH_CMD");
    TEST_ASSERT(ast->cmd.argc == 2, "p12: redirect cmd has 2 args");
    TEST_ASSERT(ast->cmd.redir_count == 1, "p12: redirect count is 1");
    TEST_ASSERT(ast->cmd.redirs[0].target_fd == 1, "p12: redirect fd is stdout");
    TEST_ASSERT(strcmp(ast->cmd.redirs[0].filename, "/tmp/out") == 0,
                "p12: redirect filename correct");

    /* Parse semicolon list */
    n = sh_tokenize("echo a ; echo b", tokens, SH_MAX_TOKENS);
    ast = sh_parse(tokens, n);
    TEST_ASSERT(ast != NULL, "p12: parse semicolon list");
    TEST_ASSERT(ast->type == SH_LIST, "p12: semicolon is SH_LIST");
    TEST_ASSERT(ast->list.count == 2, "p12: semicolon list has 2");
    TEST_ASSERT(ast->list.operators[0] == 0, "p12: operator is ; (0)");

    /* Parse for loop */
    n = sh_tokenize("for x in a b c ; do echo x ; done", tokens, SH_MAX_TOKENS);
    ast = sh_parse(tokens, n);
    TEST_ASSERT(ast != NULL, "p12: parse for loop");
    TEST_ASSERT(ast->type == SH_FOR, "p12: for loop is SH_FOR");
    TEST_ASSERT(strcmp(ast->for_node.var, "x") == 0, "p12: for var is x");
    TEST_ASSERT(ast->for_node.word_count == 3, "p12: for has 3 words");
    TEST_ASSERT(ast->for_node.body != NULL, "p12: for body exists");

    /* Parse if/then/fi */
    n = sh_tokenize("if true ; then echo yes ; fi", tokens, SH_MAX_TOKENS);
    ast = sh_parse(tokens, n);
    TEST_ASSERT(ast != NULL, "p12: parse if/then/fi");
    TEST_ASSERT(ast->type == SH_IF, "p12: if is SH_IF");
    TEST_ASSERT(ast->if_node.condition != NULL, "p12: if condition exists");
    TEST_ASSERT(ast->if_node.then_body != NULL, "p12: if then_body exists");

    /* Parse while loop */
    n = sh_tokenize("while true ; do echo loop ; done", tokens, SH_MAX_TOKENS);
    ast = sh_parse(tokens, n);
    TEST_ASSERT(ast != NULL, "p12: parse while loop");
    TEST_ASSERT(ast->type == SH_WHILE, "p12: while is SH_WHILE");
    TEST_ASSERT(ast->while_node.is_until == 0, "p12: while, not until");
    TEST_ASSERT(ast->while_node.condition != NULL, "p12: while condition exists");
    TEST_ASSERT(ast->while_node.body != NULL, "p12: while body exists");
}

static void test_phase12_shell(void) {
    printf("== Phase 12 Shell Integration Tests ==\n");

    /* Test true/false builtins via dispatch */
    char *true_argv[] = { "true" };
    int rc = shell_dispatch_command(1, true_argv);
    TEST_ASSERT(rc == 0, "p12: true returns 0");

    char *false_argv[] = { "false" };
    rc = shell_dispatch_command(1, false_argv);
    TEST_ASSERT(sh_get_exit_code() == 1, "p12: false sets exit code 1");

    /* Test variable assignment */
    char *assign_argv[] = { "TESTSHVAR=hello123" };
    rc = shell_dispatch_command(1, assign_argv);
    const char *val = env_get("TESTSHVAR");
    TEST_ASSERT(val && strcmp(val, "hello123") == 0,
                "p12: FOO=bar sets env var");
    env_unset("TESTSHVAR");

    /* Test echo via dispatch */
    /* We can't easily capture printf output in tests, but we can verify
     * the command doesn't crash and returns successfully */
    char *echo_argv[] = { "echo", "test_output" };
    rc = shell_dispatch_command(2, echo_argv);
    TEST_ASSERT(rc == 0, "p12: echo returns 0");

    /* Test test/[ command */
    char *test_true[] = { "test", "-e", "/etc/hostname" };
    shell_dispatch_command(3, test_true);
    /* File should exist in a typical ImposOS install */
    /* We just verify it doesn't crash */

    /* test: string equality */
    char *test_eq[] = { "test", "hello", "=", "hello" };
    shell_dispatch_command(4, test_eq);
    TEST_ASSERT(sh_get_exit_code() == 0, "p12: test hello = hello is true");

    char *test_neq[] = { "test", "hello", "=", "world" };
    shell_dispatch_command(4, test_neq);
    TEST_ASSERT(sh_get_exit_code() == 1, "p12: test hello = world is false");

    /* test: string inequality */
    char *test_ne[] = { "test", "a", "!=", "b" };
    shell_dispatch_command(4, test_ne);
    TEST_ASSERT(sh_get_exit_code() == 0, "p12: test a != b is true");

    /* test: numeric comparison */
    char *test_num_eq[] = { "test", "42", "-eq", "42" };
    shell_dispatch_command(4, test_num_eq);
    TEST_ASSERT(sh_get_exit_code() == 0, "p12: test 42 -eq 42 is true");

    char *test_num_lt[] = { "test", "5", "-lt", "10" };
    shell_dispatch_command(4, test_num_lt);
    TEST_ASSERT(sh_get_exit_code() == 0, "p12: test 5 -lt 10 is true");

    char *test_num_gt[] = { "test", "10", "-gt", "5" };
    shell_dispatch_command(4, test_num_gt);
    TEST_ASSERT(sh_get_exit_code() == 0, "p12: test 10 -gt 5 is true");

    /* test: -z empty string */
    char *test_z[] = { "test", "-z", "" };
    shell_dispatch_command(3, test_z);
    TEST_ASSERT(sh_get_exit_code() == 0, "p12: test -z '' is true");

    char *test_nz[] = { "test", "-n", "notempty" };
    shell_dispatch_command(3, test_nz);
    TEST_ASSERT(sh_get_exit_code() == 0, "p12: test -n notempty is true");

    /* Test command not found */
    char *bad_argv[] = { "nonexistent_command_xyz" };
    rc = shell_dispatch_command(1, bad_argv);
    TEST_ASSERT(rc == 127, "p12: unknown command returns 127");

    /* Test && execution: true && echo ok should run echo */
    sh_set_exit_code(0);
    shell_process_command("true && true");
    TEST_ASSERT(sh_get_exit_code() == 0, "p12: true && true = 0");

    shell_process_command("false && true");
    TEST_ASSERT(sh_get_exit_code() == 1, "p12: false && true = 1 (short-circuit)");

    /* Test || execution */
    shell_process_command("false || true");
    TEST_ASSERT(sh_get_exit_code() == 0, "p12: false || true = 0");

    shell_process_command("true || false");
    TEST_ASSERT(sh_get_exit_code() == 0, "p12: true || false = 0 (short-circuit)");
}

void test_shell_all(void) {
    test_phase12_tokenizer();
    test_phase12_glob();
    test_phase12_expand();
    test_phase12_parser();
    test_phase12_shell();
}
