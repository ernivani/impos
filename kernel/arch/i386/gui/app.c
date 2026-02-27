/* app.c — App registry, pin system, and launch dispatcher.
 *
 * 35+ apps organized in 7 categories.
 * Pinned apps appear in the radial launcher (max 8).
 * app_launch() dispatches to real implementations (settings, etc.)
 * or prints a placeholder message for unimplemented apps.
 */
#include <kernel/app.h>
#include <stdio.h>
#include <string.h>

/* ── App Registry ───────────────────────────────────────────────── */

static const app_info_t registry[] = {
    /* System */
    { "terminal",  "Terminal",    "Tm", ICON_TERMINAL, 0xFF1E3A5F, APP_CAT_SYSTEM,   1, "bash,shell,cli,console,cmd,tty" },
    { "files",     "Files",       "Fl", ICON_FILES,    0xFF2E7D32, APP_CAT_SYSTEM,   1, "finder,explorer,folder,directory,browse" },
    { "settings",  "Settings",    "St", ICON_SETTINGS, 0xFFFF9500, APP_CAT_SYSTEM,   1, "preferences,config,options,system,control" },
    { "monitor",   "Monitor",     "Mo", ICON_MONITOR,  0xFF00C7BE, APP_CAT_SYSTEM,   1, "task,process,cpu,memory,performance,htop,top" },
    { "disk",      "Disk Usage",  "Du", ICON_DISK,     0xFF5D4037, APP_CAT_SYSTEM,   0, "storage,space,usage,drive,hdd,ssd" },
    { "sysinfo",   "System Info", "Si", ICON_MONITOR,  0xFF263238, APP_CAT_SYSTEM,   0, "hardware,about,specs,cpu,ram" },
    { "packages",  "Packages",    "Pk", ICON_DOWNLOAD, 0xFF1B5E20, APP_CAT_SYSTEM,   0, "install,software,apt,brew,update" },
    { "users",     "Users",       "Us", ICON_USERS,    0xFF4527A0, APP_CAT_SYSTEM,   0, "accounts,people,login,password" },
    { "logs",      "Logs",        "Lg", ICON_TERMINAL, 0xFF212121, APP_CAT_SYSTEM,   0, "syslog,journal,dmesg,events" },

    /* Internet */
    { "browser",   "Browser",     "Br", ICON_BROWSER,  0xFF5856D6, APP_CAT_INTERNET, 1, "web,internet,http,url,chrome,firefox,safari" },
    { "email",     "Email",       "Em", ICON_EMAIL,    0xFF5856D6, APP_CAT_INTERNET, 0, "mail,inbox,message,smtp,imap" },
    { "chat",      "Chat",        "Ch", ICON_CHAT,     0xFF5856D6, APP_CAT_INTERNET, 0, "messenger,im,talk,irc,discord,slack" },
    { "torrent",   "Torrent",     "To", ICON_DOWNLOAD, 0xFF5856D6, APP_CAT_INTERNET, 0, "download,p2p,bittorrent,share" },
    { "ftp",       "FTP Client",  "Ft", ICON_FILES,    0xFF5856D6, APP_CAT_INTERNET, 0, "upload,sftp,transfer,remote" },

    /* Media */
    { "music",     "Music",       "Mu", ICON_MUSIC,    0xFFFF3B30, APP_CAT_MEDIA,    1, "audio,player,mp3,song,playlist,spotify" },
    { "video",     "Video Player","Vd", ICON_VIDEO,    0xFFFF3B30, APP_CAT_MEDIA,    0, "movie,film,mp4,vlc,watch,stream" },
    { "podcasts",  "Podcasts",    "Po", ICON_MUSIC,    0xFFFF3B30, APP_CAT_MEDIA,    0, "radio,listen,episode,rss" },
    { "recorder",  "Screen Rec.", "Sr", ICON_VIDEO,    0xFFFF3B30, APP_CAT_MEDIA,    0, "capture,record,screencast,obs" },
    { "images",    "Image Viewer","Iv", ICON_IMAGE,    0xFFFF3B30, APP_CAT_MEDIA,    0, "photo,picture,gallery,jpg,png" },
    { "radio",     "Radio",       "Ra", ICON_RADIO,    0xFFFF3B30, APP_CAT_MEDIA,    0, "fm,stream,broadcast,tune" },

    /* Graphics */
    { "photoeditor","Photo Editor","Pe", ICON_IMAGE,   0xFFFF9500, APP_CAT_GRAPHICS, 0, "edit,retouch,photoshop,gimp,filter" },
    { "vectordraw","Vector Draw", "Vr", ICON_PEN,      0xFFFF9500, APP_CAT_GRAPHICS, 0, "svg,illustrator,draw,inkscape,sketch" },
    { "screenshot","Screenshot",  "Sc", ICON_IMAGE,    0xFFFF9500, APP_CAT_GRAPHICS, 0, "capture,snip,grab,screen,print" },
    { "colorpick", "Color Picker","Cp", ICON_IMAGE,    0xFFFF9500, APP_CAT_GRAPHICS, 0, "hex,rgb,palette,eyedropper" },

    /* Development */
    { "code",      "Code Editor", "Ce", ICON_CODE,     0xFF34C759, APP_CAT_DEV,      0, "text,editor,vscode,vim,ide,program" },
    { "git",       "Git Client",  "Gc", ICON_CODE,     0xFF34C759, APP_CAT_DEV,      0, "version,control,repo,commit,github" },
    { "database",  "Database",    "Db", ICON_TABLE,    0xFF34C759, APP_CAT_DEV,      0, "sql,sqlite,mysql,postgres,query" },
    { "apitester", "API Tester",  "At", ICON_BROWSER,  0xFF34C759, APP_CAT_DEV,      0, "rest,http,postman,request,endpoint" },
    { "debugger",  "Debugger",    "Dg", ICON_CODE,     0xFF34C759, APP_CAT_DEV,      0, "gdb,breakpoint,step,trace,inspect" },

    /* Office */
    { "writer",    "Writer",      "Wr", ICON_PEN,      0xFFAF52DE, APP_CAT_OFFICE,   0, "document,word,doc,write,text" },
    { "spreadsheet","Spreadsheet","Sp", ICON_TABLE,    0xFFAF52DE, APP_CAT_OFFICE,   0, "excel,csv,calc,formula,table" },
    { "presenter", "Presenter",   "Pr", ICON_PDF,      0xFFAF52DE, APP_CAT_OFFICE,   0, "slides,powerpoint,keynote,presentation" },
    { "pdfreader", "PDF Reader",  "Pd", ICON_PDF,      0xFFAF52DE, APP_CAT_OFFICE,   0, "acrobat,reader,view,document" },
    { "notes",     "Notes",       "Nt", ICON_PEN,      0xFFAF52DE, APP_CAT_OFFICE,   0, "memo,todo,list,notebook,jot" },

    /* Games */
    { "solitaire", "Solitaire",   "So", ICON_GAMEPAD,  0xFF00C7BE, APP_CAT_GAMES,    0, "cards,card,patience,klondike" },
    { "mines",     "Minesweeper", "Mi", ICON_GAMEPAD,  0xFF00C7BE, APP_CAT_GAMES,    0, "bomb,sweep,puzzle,grid" },
    { "chess",     "Chess",       "Cs", ICON_GAMEPAD,  0xFF00C7BE, APP_CAT_GAMES,    0, "board,strategy,king,checkmate" },
    { "tetris",    "Tetris",      "Te", ICON_GAMEPAD,  0xFF00C7BE, APP_CAT_GAMES,    0, "blocks,puzzle,falling,classic" },
    { "snake",     "Snake",       "Sn", ICON_GAMEPAD,  0xFF00C7BE, APP_CAT_GAMES,    0, "arcade,classic,retro,worm" },
};

#define REGISTRY_COUNT ((int)(sizeof(registry) / sizeof(registry[0])))

/* ── Pin state ──────────────────────────────────────────────────── */

static int pin_slots[APP_MAX_PINNED];  /* index into registry, -1 = empty */
static int pin_count = 0;

/* ── Category metadata ──────────────────────────────────────────── */

static const char *cat_names[APP_CAT_COUNT] = {
    "System", "Internet", "Media", "Graphics", "Development", "Office", "Games"
};

static const uint32_t cat_colors[APP_CAT_COUNT] = {
    0xFF3478F6, 0xFF5856D6, 0xFFFF3B30,
    0xFFFF9500, 0xFF34C759, 0xFFAF52DE, 0xFF00C7BE
};

/* ── Public API ─────────────────────────────────────────────────── */

void app_init(void) {
    int i;
    pin_count = 0;
    for (i = 0; i < APP_MAX_PINNED; i++) pin_slots[i] = -1;

    /* Populate default pins */
    for (i = 0; i < REGISTRY_COUNT && pin_count < APP_MAX_PINNED; i++) {
        if (registry[i].default_pin) {
            pin_slots[pin_count++] = i;
        }
    }
}

int app_get_count(void) { return REGISTRY_COUNT; }

const app_info_t *app_get(int idx) {
    if (idx < 0 || idx >= REGISTRY_COUNT) return 0;
    return &registry[idx];
}

const app_info_t *app_find(const char *id) {
    int i;
    if (!id) return 0;
    for (i = 0; i < REGISTRY_COUNT; i++) {
        const char *a = registry[i].id, *b = id;
        int match = 1;
        while (*a || *b) {
            if (*a != *b) { match = 0; break; }
            a++; b++;
        }
        if (match) return &registry[i];
    }
    return 0;
}

/* Forward declarations for apps that have real implementations */
void app_settings_open_to(const char *tab);
void app_terminal_open(void);
void app_filemgr_open(void);
void app_monitor_open(void);
void app_taskmgr_open(void);

void app_launch(const char *id) {
    if (!id) return;

    /* Real apps — dispatch by first few chars of id */
    if (id[0]=='s' && id[1]=='e' && id[2]=='t') {
        app_settings_open_to("wallpaper");
        return;
    }
    if (id[0]=='t' && id[1]=='e' && id[2]=='r') {
        app_terminal_open();
        return;
    }
    if (id[0]=='f' && id[1]=='i' && id[2]=='l') {
        app_filemgr_open();
        return;
    }
    if (id[0]=='m' && id[1]=='o' && id[2]=='n') {
        /* monitor — keywords: task,process,htop,top */
        app_taskmgr_open();
        return;
    }
    if (id[0]=='s' && id[1]=='y' && id[2]=='s') {
        /* sysinfo — system stats overview */
        app_monitor_open();
        return;
    }
    if (id[0]=='d' && id[1]=='i' && id[2]=='s' && id[3]=='k') {
        /* disk — reuse monitor for now */
        app_monitor_open();
        return;
    }

    /* Everything else: log it */
    printf("[app] launch: %s\n", id);
}

int app_pin_count(void) { return pin_count; }

int app_pin_get(int slot) {
    if (slot < 0 || slot >= pin_count) return -1;
    return pin_slots[slot];
}

void app_pin_toggle(int app_idx) {
    int i;
    if (app_idx < 0 || app_idx >= REGISTRY_COUNT) return;

    /* Check if already pinned */
    for (i = 0; i < pin_count; i++) {
        if (pin_slots[i] == app_idx) {
            /* Remove: shift left */
            int j;
            for (j = i; j < pin_count - 1; j++)
                pin_slots[j] = pin_slots[j + 1];
            pin_slots[--pin_count] = -1;
            return;
        }
    }

    /* Pin if room */
    if (pin_count < APP_MAX_PINNED)
        pin_slots[pin_count++] = app_idx;
}

int app_is_pinned(int app_idx) {
    int i;
    for (i = 0; i < pin_count; i++)
        if (pin_slots[i] == app_idx) return 1;
    return 0;
}

void app_pin_reorder(int from_slot, int to_slot) {
    if (from_slot < 0 || from_slot >= pin_count) return;
    if (to_slot   < 0 || to_slot   >= pin_count) return;
    if (from_slot == to_slot) return;
    int tmp = pin_slots[from_slot];
    int dir = (to_slot > from_slot) ? 1 : -1;
    for (int i = from_slot; i != to_slot; i += dir)
        pin_slots[i] = pin_slots[i + dir];
    pin_slots[to_slot] = tmp;
}

const char *app_cat_name(int cat) {
    if (cat < 0 || cat >= APP_CAT_COUNT) return "";
    return cat_names[cat];
}

uint32_t app_cat_color(int cat) {
    if (cat < 0 || cat >= APP_CAT_COUNT) return 0xFF808080;
    return cat_colors[cat];
}
