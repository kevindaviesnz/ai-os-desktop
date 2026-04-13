#include "os_ipc.h"
#include "os_types.h"

#define ATTR_EL0       __attribute__((section(".el0_user_text")))
#define ATTR_EL0_ENTRY __attribute__((section(".el0_entry")))
#define ATTR_EL0_RO    __attribute__((section(".el0_user_rodata")))

#define COLOR_BG 0xFF1E1E1E
#define COLOR_FG 0xFF00FF00
#define CMD_MAX_LEN 256

typedef struct {
    uint32_t *framebuffer;
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t cursor_x;
    uint32_t cursor_y;
} term_ctx_t;

ATTR_EL0_RO static const uint8_t font8x8[ 95 ][ 8 ] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* (space) */
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, /* ! */
    {0x6C,0x6C,0x6C,0x00,0x00,0x00,0x00,0x00}, /* " */
    {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00}, /* # */
    {0x18,0x7E,0xC0,0x7E,0x06,0x7E,0x18,0x00}, /* $ */
    {0x00,0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00}, /* % */
    {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00}, /* & */
    {0x30,0x30,0x60,0x00,0x00,0x00,0x00,0x00}, /* ' */
    {0x18,0x30,0x60,0x60,0x60,0x30,0x18,0x00}, /* ( */
    {0x60,0x30,0x18,0x18,0x18,0x30,0x60,0x00}, /* ) */
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, /* * */
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}, /* + */
    {0x00,0x00,0x00,0x00,0x00,0x30,0x30,0x60}, /* , */
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}, /* - */
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, /* . */
    {0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00}, /* / */
    {0x7C,0xC6,0xCE,0xD6,0xE6,0xC6,0x7C,0x00}, /* 0 */
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}, /* 1 */
    {0x7C,0xC6,0x06,0x1C,0x30,0x60,0xFE,0x00}, /* 2 */
    {0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00}, /* 3 */
    {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x1E,0x00}, /* 4 */
    {0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00}, /* 5 */
    {0x38,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0x00}, /* 6 */
    {0xFE,0xC6,0x06,0x0C,0x18,0x30,0x30,0x00}, /* 7 */
    {0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00}, /* 8 */
    {0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00}, /* 9 */
    {0x00,0x30,0x30,0x00,0x00,0x30,0x30,0x00}, /* : */
    {0x00,0x30,0x30,0x00,0x00,0x30,0x30,0x60}, /* ; */
    {0x18,0x30,0x60,0xC0,0x60,0x30,0x18,0x00}, /* < */
    {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00}, /* = */
    {0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00}, /* > */
    {0x7C,0xC6,0x06,0x1C,0x18,0x00,0x18,0x00}, /* ? */
    {0x7C,0xC6,0xDE,0xDE,0xDC,0xC0,0x7C,0x00}, /* @ */
    {0x38,0x6C,0xC6,0xFE,0xC6,0xC6,0xC6,0x00}, /* A */
    {0xFC,0xC6,0xC6,0xFC,0xC6,0xC6,0xFC,0x00}, /* B */
    {0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00}, /* C */
    {0xF8,0xCC,0xC6,0xC6,0xC6,0xCC,0xF8,0x00}, /* D */
    {0xFE,0xC0,0xC0,0xFC,0xC0,0xC0,0xFE,0x00}, /* E */
    {0xFE,0xC0,0xC0,0xFC,0xC0,0xC0,0xC0,0x00}, /* F */
    {0x3C,0x66,0xC0,0xCE,0xC6,0x66,0x3E,0x00}, /* G */
    {0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00}, /* H */
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x7E,0x00}, /* I */
    {0x0E,0x06,0x06,0x06,0xC6,0xC6,0x7C,0x00}, /* J */
    {0xC6,0xCC,0xD8,0xF0,0xD8,0xCC,0xC6,0x00}, /* K */
    {0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xFE,0x00}, /* L */
    {0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0x00}, /* M */
    {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00}, /* N */
    {0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00}, /* O */
    {0xFC,0xC6,0xC6,0xFC,0xC0,0xC0,0xC0,0x00}, /* P */
    {0x7C,0xC6,0xC6,0xC6,0xDA,0xCC,0x7A,0x00}, /* Q */
    {0xFC,0xC6,0xC6,0xFC,0xD8,0xCC,0xC6,0x00}, /* R */
    {0x7C,0xC6,0xC0,0x7C,0x06,0xC6,0x7C,0x00}, /* S */
    {0xFE,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, /* T */
    {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00}, /* U */
    {0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x00}, /* V */
    {0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0x00}, /* W */
    {0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0xC6,0x00}, /* X */
    {0xC6,0xC6,0xC6,0x7C,0x18,0x18,0x18,0x00}, /* Y */
    {0xFE,0x06,0x0C,0x18,0x30,0x60,0xFE,0x00}, /* Z */
    {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00}, /* [ */
    {0x80,0xC0,0x60,0x30,0x18,0x0C,0x06,0x00}, /* \ */
    {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00}, /* ] */
    {0x18,0x3C,0x66,0x00,0x00,0x00,0x00,0x00}, /* ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00}, /* _ */
    {0x60,0x60,0x30,0x00,0x00,0x00,0x00,0x00}, /* ` */
    {0x00,0x00,0x7C,0x06,0x7E,0xC6,0x7E,0x00}, /* a */
    {0xC0,0xC0,0xFC,0xC6,0xC6,0xC6,0xFC,0x00}, /* b */
    {0x00,0x00,0x7C,0xC6,0xC0,0xC6,0x7C,0x00}, /* c */
    {0x06,0x06,0x7E,0xC6,0xC6,0xC6,0x7E,0x00}, /* d */
    {0x00,0x00,0x7C,0xC6,0xFE,0xC0,0x7C,0x00}, /* e */
    {0x1C,0x30,0xFC,0x30,0x30,0x30,0x30,0x00}, /* f */
    {0x00,0x00,0x7E,0xC6,0xC6,0x7E,0x06,0x7C}, /* g */
    {0xC0,0xC0,0xFC,0xC6,0xC6,0xC6,0xC6,0x00}, /* h */
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00}, /* i */
    {0x06,0x00,0x06,0x06,0x06,0xC6,0x7C,0x00}, /* j */
    {0xC0,0xC0,0xCC,0xD8,0xF0,0xD8,0xCC,0x00}, /* k */
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, /* l */
    {0x00,0x00,0xFC,0xFE,0xD6,0xD6,0xC6,0x00}, /* m */
    {0x00,0x00,0xFC,0xC6,0xC6,0xC6,0xC6,0x00}, /* n */
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00}, /* o */
    {0x00,0x00,0xFC,0xC6,0xC6,0xFC,0xC0,0xC0}, /* p */
    {0x00,0x00,0x7E,0xC6,0xC6,0x7E,0x06,0x06}, /* q */
    {0x00,0x00,0xDC,0x66,0x60,0x60,0x60,0x00}, /* r */
    {0x00,0x00,0x7C,0xC0,0x7C,0x06,0x7C,0x00}, /* s */
    {0x30,0x30,0xFC,0x30,0x30,0x30,0x1C,0x00}, /* t */
    {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0x7E,0x00}, /* u */
    {0x00,0x00,0xC6,0xC6,0xC6,0x6C,0x38,0x00}, /* v */
    {0x00,0x00,0xC6,0xC6,0xD6,0xFE,0x6C,0x00}, /* w */
    {0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00}, /* x */
    {0x00,0x00,0xC6,0xC6,0xC6,0x7E,0x06,0x7C}, /* y */
    {0x00,0x00,0xFE,0x0C,0x18,0x30,0xFE,0x00}, /* z */
    {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00}, /* { */
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, /* | */
    {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00}, /* } */
    {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00}  /* ~ */
};

ATTR_EL0_RO static const char msg_init[]   = "ai-os-desktop terminal subsystem initialized.\n";
ATTR_EL0_RO static const char msg_fb[]     = "Framebuffer rendering active.\n";
ATTR_EL0_RO static const char msg_wait[]   = "IPC Mailbox Online. Ready for commands.\n\n";
ATTR_EL0_RO static const char msg_prompt[] = "kevindavies@ai-os / % ";

ATTR_EL0_RO static const char cmd_help[]  = "help";
ATTR_EL0_RO static const char cmd_clear[] = "clear";

ATTR_EL0_RO static const char msg_help_1[] = "Available commands:\n";
ATTR_EL0_RO static const char msg_help_2[] = "  help      - Show this message\n";
ATTR_EL0_RO static const char msg_help_3[] = "  clear     - Clear the terminal screen\n";
ATTR_EL0_RO static const char msg_help_4[] = "  s.list    - List directory contents (alias: s.ls)\n";
ATTR_EL0_RO static const char msg_help_5[] = "  s.read    - Print file contents (Usage: s.read <file>)\n";
ATTR_EL0_RO static const char msg_help_6[] = "  s.write   - Write file (Usage: s.write <file> <data>)\n";
ATTR_EL0_RO static const char msg_help_7[] = "  agent.why - View OS short-term memory logic\n"; 
ATTR_EL0_RO static const char msg_help_8[] = "  atk.run   - Execute Autarky bytecode (.atk)\n"; 
ATTR_EL0_RO static const char msg_help_9[] = "  ledger.sync - Persist VM history to LEDGER.LOG\n"; /* NEW: Phase 15 Help */
ATTR_EL0_RO static const char msg_unknown[] = "Unknown command: ";

ATTR_EL0_RO static const char ns_storage[] = "storage";
ATTR_EL0_RO static const char ns_s[]       = "s";
ATTR_EL0_RO static const char ns_agent[]   = "agent"; 
ATTR_EL0_RO static const char ns_atk[]     = "atk";
ATTR_EL0_RO static const char ns_ledger[]  = "ledger"; /* NEW: Phase 15 Namespace */
ATTR_EL0_RO static const char verb_list[]  = "list";
ATTR_EL0_RO static const char verb_ls[]    = "ls";
ATTR_EL0_RO static const char verb_read[]  = "read";
ATTR_EL0_RO static const char verb_write[] = "write";
ATTR_EL0_RO static const char verb_why[]   = "why";   
ATTR_EL0_RO static const char verb_run[]   = "run";
ATTR_EL0_RO static const char verb_sync[]  = "sync";  /* NEW: Phase 15 Verb */

ATTR_EL0_RO static const char err_usage_write[]  = "Usage: s.write <filename> <data>\n";
ATTR_EL0_RO static const char err_unk_verb[]     = "Unknown storage verb.\n";
ATTR_EL0_RO static const char err_unk_ns[]       = "Unknown namespace.\n";
ATTR_EL0_RO static const char err_unk_agent_verb[] = "Unknown agent verb.\n";

ATTR_EL0 static int shell_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

ATTR_EL0 static char *shell_strchr(const char *str, int c) {
    while (*str) {
        if (*str == (char)c) return (char *)str;
        str++;
    }
    return 0;
}

ATTR_EL0 void sys_ipc_send(os_message_t *msg) {
    register uint64_t x0 __asm__("x0") = (uint64_t)msg;
    register uint64_t x8 __asm__("x8") = SYS_IPC_SEND;
    __asm__ volatile("svc 0" : "+r"(x0) : "r"(x8) : "memory");
}

ATTR_EL0 void ipc_receive(os_message_t *msg) {
    register uint64_t x0 __asm__("x0") = (uint64_t)msg;
    register uint64_t x8 __asm__("x8") = SYS_IPC_RECV;
    __asm__ volatile("svc 0" : "+r"(x0) : "r"(x8) : "memory");
}

ATTR_EL0 void sys_gpu_flush(void) {
    register uint64_t x8 __asm__("x8") = SYS_GPU_FLUSH;
    __asm__ volatile("svc 0" : : "r"(x8) : "memory");
}

ATTR_EL0 void sys_hw_drain(void) {
    register uint64_t x8 __asm__("x8") = SYS_HW_DRAIN;
    __asm__ volatile("svc 0" : : "r"(x8) : "memory");
}

ATTR_EL0 void term_clear_screen(term_ctx_t *ctx) {
    for (uint32_t i = 0; i < ctx->screen_width * ctx->screen_height; i++) {
        ctx->framebuffer[ i ] = COLOR_BG;
    }
    ctx->cursor_x = 0;
    ctx->cursor_y = 0;
}

ATTR_EL0 void term_draw_char(term_ctx_t *ctx, char c, uint32_t x, uint32_t y) {
    if (c < 32 || c > 126) return;
    const uint8_t *glyph = font8x8[ (int)c - 32 ];
    for (int row = 0; row < 8; row++) {
        uint8_t row_data = glyph[ row ];
        for (int col = 0; col < 8; col++) {
            if (row_data & (1 << (7 - col))) {
                for (int dy = 0; dy < 2; dy++) {
                    for (int dx = 0; dx < 2; dx++) {
                        uint32_t px = x + (col * 2) + dx;
                        uint32_t py = y + (row * 2) + dy;
                        if (px < ctx->screen_width && py < ctx->screen_height) {
                            ctx->framebuffer[ py * ctx->screen_width + px ] = COLOR_FG;
                        }
                    }
                }
            }
        }
    }
}

ATTR_EL0 void term_scroll(term_ctx_t *ctx) {
    uint32_t char_height   = 16;
    uint32_t *dest         = ctx->framebuffer;
    uint32_t *src          = ctx->framebuffer + (ctx->screen_width * char_height);
    uint32_t words_to_move = ctx->screen_width * (ctx->screen_height - char_height);
    for (uint32_t i = 0; i < words_to_move; i++) {
        dest[ i ] = src[ i ];
    }
    for (uint32_t i = words_to_move; i < ctx->screen_width * ctx->screen_height; i++) {
        ctx->framebuffer[ i ] = COLOR_BG;
    }
    ctx->cursor_y -= char_height;
}

ATTR_EL0 void term_putc(term_ctx_t *ctx, char c) {
    uint32_t char_width  = 16;
    uint32_t char_height = 16;

    if (c == '\n') {
        ctx->cursor_x  = 0;
        ctx->cursor_y += char_height;
    } else if (c == '\b') {
        if (ctx->cursor_x >= char_width) {
            ctx->cursor_x -= char_width;
            for (uint32_t dy = 0; dy < char_height; dy++) {
                for (uint32_t dx = 0; dx < char_width; dx++) {
                    ctx->framebuffer[ (ctx->cursor_y + dy) * ctx->screen_width
                                    + (ctx->cursor_x + dx) ] = COLOR_BG;
                }
            }
        }
    } else {
        term_draw_char(ctx, c, ctx->cursor_x, ctx->cursor_y);
        ctx->cursor_x += char_width;
        if (ctx->cursor_x + char_width > ctx->screen_width) {
            ctx->cursor_x  = 0;
            ctx->cursor_y += char_height;
        }
    }

    if (ctx->cursor_y + char_height > ctx->screen_height) {
        term_scroll(ctx);
    }
}

ATTR_EL0 void term_print(term_ctx_t *ctx, const char *str) {
    while (*str) {
        term_putc(ctx, *str++);
    }
}

ATTR_EL0_ENTRY int shell_main(void) {
    term_ctx_t ctx;
    ctx.framebuffer   = (uint32_t *)0x20000000;
    ctx.screen_width  = 1280;
    ctx.screen_height = 800;
    ctx.cursor_x      = 0;
    ctx.cursor_y      = 0;

    term_clear_screen(&ctx);
    term_print(&ctx, msg_init);
    term_print(&ctx, msg_fb);
    term_print(&ctx, msg_wait);
    term_print(&ctx, msg_prompt);

    sys_gpu_flush();

    char cmd_buffer[ CMD_MAX_LEN ];
    uint32_t cmd_idx = 0;

    while (1) {
        sys_hw_drain();
        
        os_message_t msg;
        ipc_receive(&msg);

        if (msg.type == IPC_MSG_KEY_PRESS && msg.length > 0) {
            char c = (char)msg.payload[ 0 ];

            if (c == '\n') {
                term_putc(&ctx, c);
                sys_gpu_flush();
                cmd_buffer[ cmd_idx ] = '\0';

                if (cmd_idx > 0) {
                    char *dot = shell_strchr(cmd_buffer, '.');
                    if (dot) {
                        *dot = '\0'; 
                        char *verb = dot + 1;
                        char *args = shell_strchr(verb, ' ');
                        if (args) {
                            *args = '\0'; 
                            args++;       
                        }

                        if (shell_strcmp(cmd_buffer, ns_storage) == 0 || shell_strcmp(cmd_buffer, ns_s) == 0) {
                            if (shell_strcmp(verb, verb_list) == 0 || shell_strcmp(verb, verb_ls) == 0) {
                                os_message_t req;
                                req.sender_id = SYS_MOD_GUI_SHELL;
                                req.target_id = SYS_MOD_KERNEL;
                                req.type = IPC_MSG_FS_LIST_REQ;
                                req.length = 0;
                                sys_ipc_send(&req);

                                int waiting = 1;
                                while (waiting) {
                                    os_message_t resp;
                                    ipc_receive(&resp);
                                    if (resp.type == IPC_MSG_FS_LIST_RESP) {
                                        term_print(&ctx, (char *)resp.payload);
                                        waiting = 0;
                                    }
                                }
                                sys_hw_drain();
                            }
                            else if (shell_strcmp(verb, verb_read) == 0 && args != 0) {
                                os_message_t req;
                                req.sender_id = SYS_MOD_GUI_SHELL;
                                req.target_id = SYS_MOD_KERNEL;
                                req.type = IPC_MSG_FS_READ_REQ;
                                int i = 0;
                                while (args[ i ] != '\0' && i < 255) { req.payload[ i ] = args[ i ]; i++; }
                                req.payload[ i ] = '\0'; req.length = i + 1;
                                sys_ipc_send(&req);

                                int waiting = 1;
                                while (waiting) {
                                    os_message_t resp;
                                    ipc_receive(&resp);
                                    if (resp.type == IPC_MSG_FS_READ_RESP) {
                                        term_print(&ctx, (char *)resp.payload);
                                        term_putc(&ctx, '\n');
                                        waiting = 0;
                                    }
                                }
                                sys_hw_drain();
                            }
                            else if (shell_strcmp(verb, verb_write) == 0 && args != 0) {
                                char *filename = args;
                                char *data = shell_strchr(args, ' ');
                                if (data) {
                                    *data = '\0'; data++;       
                                    os_message_t req;
                                    req.sender_id = SYS_MOD_GUI_SHELL;
                                    req.target_id = SYS_MOD_KERNEL;
                                    req.type = IPC_MSG_FS_WRITE_REQ;
                                    int i = 0;
                                    while (filename[ i ] != '\0' && i < 255) { req.payload[ i ] = filename[ i ]; i++; }
                                    req.payload[ i ] = '\0'; i++; 
                                    int data_start = i;
                                    while (data[ i - data_start ] != '\0' && i < 254) { req.payload[ i ] = data[ i - data_start ]; i++; }
                                    req.payload[ i ] = '\0'; req.length = i; 
                                    sys_ipc_send(&req);

                                    int waiting = 1;
                                    while (waiting) {
                                        os_message_t resp;
                                        ipc_receive(&resp);
                                        if (resp.type == IPC_MSG_FS_WRITE_RESP) {
                                            term_print(&ctx, (char *)resp.payload);
                                            waiting = 0;
                                        }
                                    }
                                    sys_hw_drain();
                                } else { term_print(&ctx, err_usage_write); }
                            } else { term_print(&ctx, err_unk_verb); }
                        } 
                        else if (shell_strcmp(cmd_buffer, ns_agent) == 0) {
                            if (shell_strcmp(verb, verb_why) == 0) {
                                os_message_t req;
                                req.sender_id = SYS_MOD_GUI_SHELL;
                                req.target_id = SYS_MOD_KERNEL;
                                req.type = IPC_MSG_WATCHER_DUMP_REQ;
                                req.length = 0;
                                sys_ipc_send(&req);
                                int waiting = 1;
                                while (waiting) {
                                    os_message_t resp;
                                    ipc_receive(&resp);
                                    if (resp.type == IPC_MSG_WATCHER_DUMP_RESP) {
                                        term_print(&ctx, (char *)resp.payload);
                                        waiting = 0;
                                    }
                                }
                                sys_hw_drain();
                            } else { term_print(&ctx, err_unk_agent_verb); }
                        }
                        else if (shell_strcmp(cmd_buffer, ns_atk) == 0) {
                            if (shell_strcmp(verb, verb_run) == 0 && args != 0) {
                                os_message_t req;
                                req.sender_id = SYS_MOD_GUI_SHELL;
                                req.target_id = SYS_MOD_KERNEL;
                                req.type = IPC_MSG_ATK_EXEC_REQ;
                                int i = 0;
                                while (args[ i ] != '\0' && i < 255) { req.payload[ i ] = args[ i ]; i++; }
                                req.payload[ i ] = '\0'; req.length = i + 1;
                                sys_ipc_send(&req);
                                int waiting = 1;
                                while (waiting) {
                                    os_message_t resp;
                                    ipc_receive(&resp);
                                    if (resp.type == IPC_MSG_ATK_EXEC_RESP) {
                                        term_print(&ctx, (char *)resp.payload);
                                        term_putc(&ctx, '\n');
                                        waiting = 0;
                                    }
                                }
                                sys_hw_drain();
                            } else { term_print(&ctx, "Usage: atk.run <filename>\n"); }
                        }
                        else if (shell_strcmp(cmd_buffer, ns_ledger) == 0) {
                            /* PHASE 15: LEDGER.SYNC Wiring */
                            if (shell_strcmp(verb, verb_sync) == 0) {
                                os_message_t req;
                                req.sender_id = SYS_MOD_GUI_SHELL;
                                req.target_id = SYS_MOD_KERNEL;
                                req.type = IPC_MSG_WATCHER_SYNC_REQ; /* Ensure this is defined in os_ipc.h */
                                req.length = 0;
                                sys_ipc_send(&req);

                                int waiting = 1;
                                while (waiting) {
                                    os_message_t resp;
                                    ipc_receive(&resp);
                                    if (resp.type == IPC_MSG_WATCHER_SYNC_RESP) {
                                        term_print(&ctx, (char *)resp.payload);
                                        waiting = 0;
                                    }
                                }
                                sys_hw_drain();
                            } else {
                                term_print(&ctx, "Unknown ledger verb.\n");
                            }
                        }
                        else { term_print(&ctx, err_unk_ns); }
                    } 
                    else if (shell_strcmp(cmd_buffer, cmd_clear) == 0) {
                        term_clear_screen(&ctx);
                    }
                    else if (shell_strcmp(cmd_buffer, cmd_help) == 0) {
                        term_print(&ctx, msg_help_1);
                        term_print(&ctx, msg_help_2);
                        term_print(&ctx, msg_help_3);
                        term_print(&ctx, msg_help_4);
                        term_print(&ctx, msg_help_5);
                        term_print(&ctx, msg_help_6);
                        term_print(&ctx, msg_help_7); 
                        term_print(&ctx, msg_help_8);
                        term_print(&ctx, msg_help_9); 
                    }
                    else {
                        term_print(&ctx, msg_unknown);
                        term_print(&ctx, cmd_buffer);
                        term_putc(&ctx, '\n');
                    }
                }
                cmd_idx = 0; 
                term_print(&ctx, msg_prompt);
            } 
            else if (c == '\b') {
                if (cmd_idx > 0) { cmd_idx--; term_putc(&ctx, c); }
            } 
            else {
                if (cmd_idx < CMD_MAX_LEN - 1) { cmd_buffer[ cmd_idx++ ] = c; term_putc(&ctx, c); }
            }
            sys_gpu_flush();
        }
    }
    return 0;
}