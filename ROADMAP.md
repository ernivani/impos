# ImposOS - Progression du développement

## Prochaines étapes (ce qu'il faut faire maintenant)

### Option 1 : Compléter les fondations réseau (recommandé)
1. **TCP** — implémenter TCP pour le port forwarding (Phase 9, tâches N.13–N.19) — grosse pièce
2. **Compléments optionnels** — libc L.10 (fprintf/fscanf/sscanf), FS F.17 (quotas)

### Option 2 : Finitions et déploiement
1. **Phase 15** — Port forwarding et services réseau (TCP requis)
2. **Phase 17** — OS « outil fonctionnel » (README, boot propre, doc, démo)
3. **Phase 16** — Accès Windows (VM ou Wine) — long terme

---

## État actuel (vue rapide)

| Ce qui marche | Ce qui manque |
|---------------|---------------|
| ✅ Boot i386, VGA 80×25 + **mode graphique VBE (1024×768, 32bpp)** | ❌ fprintf/fscanf/sscanf (libc, optionnel) |
| ✅ **Desktop environment** : splash, login GUI, dock, horloge, terminal en GUI | ❌ F.17 optionnel (quotas) |
| ✅ Shell avec **36 commandes** (dont logout, su, shutdown, gfxdemo) | ❌ TCP, UDP, sockets (réseau) |
| ✅ FS (fichiers, répertoires, persistance, permissions, chmod, chown, symlinks, blocs indirects, validation) | ❌ Port forwarding, Windows |
| ✅ Réseau L2/L3/ICMP (ping fonctionne) | |
| ✅ Users + groupes, login au boot (GUI ou texte), su | |
| ✅ libc complète (atol/atoll, stdarg, rand, qsort, bsearch, abs) | |
| ✅ **ACPI** : shutdown propre via S5 sleep state | |

**Étapes : ~123 faites / ~150 total → ~27 restantes**

---

## Résumé exécutif (détaillé)

| Indicateur | Phase | État | Détails |
|------------|-------|------|---------|
| **Étapes faites / total** | — | ~123 / ~150 | Fondations + graphique + GUI complets ; ~27 étapes restantes (Phase 3: 1 optionnel, Phase 6: 1 optionnel, Phase 9: 7, Phases 15–17: 17) |
| **Phase actuelle** | 14 | GUI (I) | Desktop environment complet avec login GUI, dock, horloge, terminal |
| **Boot / Noyau** | 1 | 6/6 | Multiboot, linker, crt, kernel_main, ISO, QEMU |
| **Affichage (VGA)** | 2 | 6/6 | VGA 80×25, TTY, scroll, curseur, couleurs |
| **libc** | 3 | quasi complet | **Fait :** string basique, printf, putchar, getchar, setjmp, exit, malloc/free/calloc/realloc, sprintf, atoi/atol/atoll, strtol, strstr, strdup/strndup, strrchr, strnlen, memchr, strcspn, strspn, strpbrk, rand/srand, qsort, bsearch, abs/div/ldiv, stdarg (GCC builtins). **Optionnel :** fprintf/fscanf/sscanf (L.10). |
| **Clavier** | 4 | 3/3 | Scan codes, getchar, AZERTY/QWERTY (setlayout) |
| **Disque (ATA)** | 5 | 3/3 | ATA read/write/flush, détection |
| **Système de fichiers** | 6 | quasi complet | **Fait :** format, load/sync, CRUD, chemins, persistance, uid/gid/mode, chmod/chown, symlinks, droits, blocs indirects, validation au load. **Optionnel :** F.17 (quotas). |
| **Shell** | 7 | 10/10 | Boucle, **36 commandes** (dont chmod, chown, useradd, userdel, logout, shutdown, gfxdemo), historique, Tab, PS1 |
| **Éditeur (vi)** | 8 | 3/3 | vi modal (ouvrir, éditer, sauver) — **fonctionne en VGA texte et en mode graphique** |
| **Réseau** | 9 | partiel | **Fait :** PCI, RTL8139, Ethernet, ARP, IP, ICMP (ping), ifconfig, lspci, arp. **Pas fini :** TCP, UDP, DNS, DHCP, IRQ (polling), pas de stack applicative. |
| **Utilisateurs** | 10 | complet | /etc/passwd, /etc/group, hash, setup initial, session, whoami, useradd, userdel, groupes, chmod, chown, droits fichiers, login (texte + **GUI**), logout, su. |
| **Config** | 11 | 7/7 | /etc/config, hostname, env, historique, timedatectl |
| **Build** | 12 | 5/5 | build.sh, ISO, make run, disque, tests de régression |
| **API graphique** | 13 | **5/6** | **VBE/VESA 32bpp, module gfx (init/width/height/bpp/pitch), double buffering (backbuf + flip/flip_rect), primitives (put_pixel/fill_rect/draw_rect/draw_line), clear + police bitmap 8×16 + draw_char/draw_string.** G.6 optionnel (alpha) partiel (nobg). |
| **Interface GUI** | 14 | **6/7** | **Thème mono noir, panneaux (rounded rect, cercles), layout HUD (dock + horloge + zone centrale), widget horloge (heure + date + infos), compositeur (splash → login → desktop → terminal), terminal en GUI (window region).** I.7 optionnel (animations) partiel (splash fade). |
| **Port forwarding** | 15 | 0/4 | TCP, serveur démo, hostfwd QEMU — **à faire** |
| **Windows (VM/Wine)** | 16 | 0/7 | Option A (VM) ou B (Wine) — **à faire** |
| **Outil fonctionnel** | 17 | 0/6 | README, boot propre, messages FR, doc, démo — **à faire** |

### Ce qui reste à faire (vue d'ensemble)

| Bloc | Phase | Étapes / compléments restants | Ordre de grandeur |
|------|-------|------------------------------|-------------------|
| **Compléments libc** | 3 | 1 tâche optionnelle : fprintf/fscanf/sscanf (L.10) | optionnel |
| **Compléments FS (F.17)** | 6 | 1 tâche optionnelle : quotas | optionnel |
| **Compléments réseau (N.13–N.19)** | 9 | 7 tâches : IRQ, UDP, TCP, DNS/DHCP optionnel, API socket | TCP = grosse pièce (semaines) |
| **API graphique (G.6)** | 13 | 1 tâche optionnelle : transparence alpha | optionnel |
| **Interface graphique (I.7)** | 14 | 1 tâche optionnelle : animations (fade, slide) pour panneaux | optionnel |
| **TCP + Port forwarding (P)** | 15 | 4 étapes | 2–4 sem (TCP = grosse pièce) |
| **Windows : VM (W.A)** | 16 | 3 étapes | 2–4 sem |
| **Windows : Wine (W.B)** | 16 | 4 étapes | plusieurs mois à années |
| **Outil fonctionnel (O)** | 17 | 6 étapes | 1–2 sem |
| **Autres (multitâche, drivers, etc.)** | — | Tout OS complet en a des dizaines | — |

En résumé : les fondations **et le graphique** sont en place. Shell 36 commandes, FS complet (permissions/symlinks/blocs indirects/validation), réseau L2/L3/ICMP, users + groupes + login GUI/texte + su, libc complète, **API graphique VBE 32bpp avec double buffering**, **desktop environment avec splash/login/dock/horloge/terminal en GUI**, ACPI shutdown, tests de régression. Il reste TCP, puis port forwarding, Windows, outil.

### Bloqueurs critiques

1. **TCP manquant** pour le port forwarding : seule la couche ICMP est en place ; TCP est une implémentation conséquente.
2. Pas de bloquant pour les phases graphiques (13–14 complétées).

### Ce qui fonctionne (état actuel = desktop environment + shell + FS + réseau L2/L3/ICMP + users)

- Boot i386 (Multiboot) et chargement du kernel à 1 MiB.
- Affichage mode texte VGA 80×25 avec couleurs, scroll, curseur.
- **Mode graphique VBE/VESA** : framebuffer 32bpp (1024×768 par défaut), double buffering, primitives 2D (put_pixel, fill_rect, draw_rect, draw_line), police bitmap 8×16, draw_char/draw_string (avec et sans fond).
- **Desktop environment** : splash screen animé (fade + spinner), login GUI (avatar, sélection utilisateur, champ mot de passe), setup wizard graphique (hostname, root, utilisateur), dock avec 7 icônes colorées (Files, Terminal, Browser, Editor, Settings, Monitor, Power), horloge (heure + date + jour de la semaine), terminal en GUI (window region), thème mono noir.
- **libc** : string basique, printf, putchar, getchar, setjmp, exit, malloc/free/calloc/realloc, sprintf, atoi, atol, atoll, strtol, strstr, strdup, strndup, strrchr, strnlen, memchr, strcspn, strspn, strpbrk, rand/srand, qsort, bsearch, abs/div/ldiv, stdarg (GCC builtins).
- Clavier (polling), getchar, deux layouts (AZERTY/QWERTY) via `setlayout` et config.
- Driver ATA : lecture/écriture secteurs, flush, détection disque.
- **Système de fichiers** : superbloc, inodes, répertoires, chemins, fs_load/fs_sync (avec validation superbloc/inodes/cwd), persistance, uid/gid/mode, chmod/chown, symlinks, vérification des droits, blocs indirects (fichiers > 32 Ko).
- Shell avec **36 commandes** : help, man, echo, cat, ls, cd, pwd, touch, mkdir, rm, clear, history, vi, sync, exit, logout, shutdown, timedatectl, ifconfig, ping, lspci, arp, export, env, whoami, chmod, chown, useradd, userdel, setlayout, su, id, ln, readlink, test, gfxdemo.
- Éditeur de ligne : backspace, Ctrl+U/etc., historique, complétion Tab, prompt PS1 avec \w et couleurs.
- Éditeur vi modal : ouvrir fichier, éditer, sauvegarder — **fonctionne en VGA texte et en mode graphique**.
- **ACPI** : détection RSDP, parsing tables, shutdown propre via S5 sleep state (commande `shutdown`).
- **Réseau L2/L3/ICMP uniquement** : PCI, RTL8139 (TX/RX), Ethernet, ARP (cache), IP, ICMP (ping). Pas de TCP/UDP.
- **Utilisateurs et groupes** : /etc/passwd, /etc/group (sel + hash), user_create, useradd, userdel, setup initial, session (USER, HOME, PS1), whoami, chmod, chown, vérification droits fichiers, login au boot (GUI ou shell_login), logout, su.
- Config : /etc/config (clavier, date/heure, timezone, 24h), hostname sur disque, variables d'environnement, historique persistant, timedatectl.
- Build : build.sh (libc + kernel), ISO GRUB, make run (QEMU + CD-ROM + disque IDE + RTL8139), tests de régression (~107 tests : user, group, FS, FS indirect, libc string/stdlib extra).

### Vue détaillée des tâches restantes

**Prochaine étape recommandée :**
- Phase 9 (TCP) → Phase 15 (port forwarding) → Phase 17 (outil fonctionnel) → Phase 16 (Windows)
- Les phases graphiques (13–14) sont complétées.

---

## PHASE 1 : Boot et noyau minimal - COMPLÉTÉ

- [x] **B.1** Écrire le linker script (sections .text, .rodata, .data, .bss à 1 MiB, entrée _start)
- [x] **B.2** Écrire boot.S : en-tête Multiboot (magic, flags, checksum), stack réservée, _start → call _init puis kernel_main
- [x] **B.3** Créer crti.S / crtn.S pour les constructeurs globaux (_init)
- [x] **B.4** kernel_main minimal qui appelle une première routine (ex. terminal_initialize puis boucle)
- [x] **B.5** Makefile kernel : compiler .c/.S, lier avec linker.ld, produire myos.kernel
- [x] **B.6** Générer une ISO (GRUB) avec menuentry multiboot, tester dans QEMU (écran noir ou message = boot OK)

---

## PHASE 2 : Affichage mode texte (VGA) - COMPLÉTÉ

- [x] **V.1** Driver VGA mode texte : adresse 0xB8000, 80×25, attribut couleur (fg | bg << 4)
- [x] **V.2** terminal_initialize : effacer l'écran, couleur par défaut, curseur en (0,0)
- [x] **V.3** terminal_putchar : afficher un caractère, gérer \n (nouvelle ligne + scroll si besoin)
- [x] **V.4** terminal_setcolor, terminal_putentryat ; scroll quand on dépasse la dernière ligne
- [x] **V.5** Mise à jour du curseur matériel (ports 0x3D4 / 0x3D5)
- [x] **V.6** Brancher putchar (et donc printf) du kernel vers le TTY pour voir du texte au boot

---

## PHASE 3 : libc - PARTIEL (minimal fait, reste à faire)

### Fait
- [x] **L.1** string (basique) : memcpy, memset, memmove, memcmp, strlen, strcmp, strcpy, strncpy, strcat, strncmp, strchr, strtok
- [x] **L.2** putchar (→ TTY), getchar (lecture clavier)
- [x] **L.3** printf (format %s %d %x %c), snprintf, puts
- [x] **L.4** setjmp / longjmp (asm)
- [x] **L.5** exit, abort (dans le kernel : longjmp vers le shell ou boucle)

### Fait — mémoire dynamique
- [x] **L.6** malloc (allocation bloc libre)
- [x] **L.7** free (libération)

### Fait — mémoire dynamique (suite)
- [x] **L.8** realloc, calloc

### Fait — stdio supplémentaires
- [x] **L.9** sprintf
- [ ] **L.10** fprintf, fscanf, sscanf (si support fichiers/streams)

### Fait — conversion nombres / string
- [x] **L.11** atoi, atol, atoll
- [x] **L.12** strtol (strtoul, strtoll, strtoull à faire)

### Fait — string supplémentaires
- [x] **L.13** strstr (recherche sous-chaîne)
- [x] **L.15** strdup, strndup

### Fait — string supplémentaires
- [x] **L.14** strrchr
- [x] **L.16** strnlen
- [x] **L.17** memchr
- [x] **L.18** strcspn, strspn, strpbrk

### Fait — variadiques
- [x] **L.19** stdarg complet via GCC builtins (__builtin_va_list, __builtin_va_start, __builtin_va_arg, __builtin_va_end, __builtin_va_copy)

### Fait — stdlib / autres
- [x] **L.20** rand, srand
- [x] **L.21** qsort, bsearch
- [x] **L.22** abs, div, ldiv

---

## PHASE 4 : Clavier - COMPLÉTÉ

- [x] **K.1** Lire les scan codes (port I/O clavier), mapper vers caractères ASCII
- [x] **K.2** Intégrer getchar() avec polling clavier (boucle d'attente touche)
- [x] **K.3** Gérer au moins deux layouts (AZERTY / QWERTY) et une commande ou config pour choisir

---

## PHASE 5 : Disque (ATA) - COMPLÉTÉ

- [x] **D.1** Driver ATA : ports 0x1F0, LBA28, ata_read_sectors, ata_write_sectors
- [x] **D.2** Attente BSY/DRQ, gestion d'erreur basique, ata_is_available
- [x] **D.3** ata_flush pour vider le cache d'écriture

---

## PHASE 6 : Système de fichiers - PARTIEL (minimal fonctionnel)

### Fait
- [x] **F.1** Définir le format : superbloc (magic, nb inodes, nb blocs), bitmaps, zone inodes, zone données ; mapping secteurs disque ↔ blocs
- [x] **F.2** alloc_inode, alloc_block, free_inode, free_block
- [x] **F.3** Inodes (type fichier/répertoire, taille, blocs directs) ; répertoires = liste (nom, inode), . et ..
- [x] **F.4** fs_initialize : si disque présent, fs_load (lire superbloc, inodes, bitmaps)
- [x] **F.5** Parser les chemins (absolus, relatifs, . et ..)
- [x] **F.6** fs_create_file (fichier ou répertoire), fs_write_file, fs_read_file
- [x] **F.7** fs_delete_file, fs_list_directory, fs_change_directory, fs_get_cwd
- [x] **F.8** fs_sync : écrire superbloc, inodes et blocs modifiés sur le disque

### À faire — permissions et propriété
- [x] **F.9** Ajouter uid, gid (ou équivalent) dans la structure inode
- [x] **F.10** Ajouter mode / permissions (bits rwx ou type) dans l’inode
- [x] **F.11** fs_chmod : changer les permissions d’un fichier
- [x] **F.12** fs_chown, fs_chgrp : changer propriétaire et groupe
- [x] **F.13** Vérifier les droits (uid/gid/mode) avant read, write, delete, list

### Fait — liens
- [x] **F.14** Symlinks : type inode symlink, stocker la cible, résoudre au moment du path lookup

### Fait — tailles
- [x] **F.15** Support fichiers plus gros : blocs indirects (single-indirect) pour dépasser la taille directe (MAX_FILE_SIZE étendu)

### Fait — robustesse
- [x] **F.16** Validation au load : superbloc (num_inodes, num_blocks), inodes actifs (type, num_blocks, blocs, indirect_block), cwd_inode fallback
- [ ] **F.17** (Optionnel) Quotas ou limites par utilisateur

---

## PHASE 7 : Shell - COMPLÉTÉ

- [x] **S.1** Boucle infinie : afficher un prompt, lire une ligne (getchar jusqu'à \n), parser les mots (argv)
- [x] **S.2** Table de commandes (nom → fonction) ; exécuter la commande si trouvée, sinon message d'erreur
- [x] **S.3** Commandes de base : echo, cat, ls, cd, pwd (en appelant fs_*)
- [x] **S.4** touch, mkdir, rm (fs_create_file, fs_delete_file)
- [x] **S.5** clear (effacer l'écran), help, man (texte intégré)
- [x] **S.6** Éditeur de ligne : backspace, raccourcis (ex. Ctrl+U pour effacer la ligne)
- [x] **S.7** Historique des commandes (ring buffer), navigation (flèches ou équivalent)
- [x] **S.8** Complétion Tab : noms de commandes + noms de fichiers du répertoire courant
- [x] **S.9** exit (retour au prompt via longjmp), shutdown (optionnel)
- [x] **S.10** Prompt configurable (ex. PS1) avec expansion (ex. \w = répertoire courant) et couleurs

---

## PHASE 8 : Éditeur de texte (vi minimal) - COMPLÉTÉ

- [x] **E.1** Commande vi FILE : ouvrir le fichier (fs_read_file), afficher le contenu
- [x] **E.2** Mode édition : déplacer le curseur, modifier le texte en mémoire
- [x] **E.3** Sauvegarder (fs_write_file) et quitter

---

## PHASE 9 : Réseau (matériel + protocoles + config) - PARTIEL (L2/L3/ICMP uniquement)

### Matériel et bas niveau
- [x] **N.1** Énumérer le bus PCI (lire config space, vendor/device ID)
- [x] **N.2** Détecter la carte RTL8139 (vendor 0x10ec, device 0x8139), initialiser (reset, buffers RX/TX, adresse MAC)
- [x] **N.3** Envoyer un paquet brut (net_send_packet), recevoir (net_receive_packet) par polling

### Protocoles (couches basses)
- [x] **N.4** Frame Ethernet : adresses MAC dest/src, type (0x0806 ARP, 0x0800 IP)
- [x] **N.5** ARP : construire requête et réponse ; cache IP → MAC avec timeout
- [x] **N.6** IP : en-tête (version, longueur, TTL, checksum, IP src/dst), démultiplexer par protocole
- [x] **N.7** ICMP : echo request (type 8), echo reply (type 0), checksum
- [x] **N.8** Commande ping IP : envoyer echo request, attendre reply (avec timeout)

### Config et commandes
- [x] **N.9** Stocker la config (IP, netmask, gateway, MAC, lien up/down)
- [x] **N.10** Commande ifconfig : afficher la config ; ifconfig eth0 IP netmask pour modifier ; up/down
- [x] **N.11** Commande lspci : lister les périphériques PCI
- [x] **N.12** Commande arp IP : envoyer une requête ARP et afficher la réponse

### À faire — couche transport et au-dessus
- [ ] **N.13** Gestion des IRQ RTL8139 (remplacer le polling par interruptions)
- [ ] **N.14** UDP : en-tête, envoi, réception, démultiplexage par port
- [ ] **N.15** TCP : en-tête, machine à états (LISTEN, SYN_SENT, ESTABLISHED, etc.), handshake, envoi/réception de flux, ACK, fermeture
- [ ] **N.16** (Optionnel) DNS client : résolution nom d’hôte → IP (requête/réponse DNS)
- [ ] **N.17** (Optionnel) DHCP client : obtenir IP, netmask, gateway automatiquement

### À faire — stack applicative
- [ ] **N.18** API ou couche « socket » (ou équivalent) pour ouvrir une connexion TCP/UDP (port local, IP/port distant)
- [ ] **N.19** Accepter une connexion entrante (serveur) et envoyer/recevoir des données

---

## PHASE 10 : Utilisateurs et authentification - PARTIEL (groupes et permissions faits)


- [x] **U.1** Format /etc/passwd (ex. username:salt_hex:hash_hex:uid:gid:home)
- [x] **U.2** Fonction de hachage (sel + hash) pour les mots de passe, vérification à la connexion
- [x] **U.3** user_load : lire /etc/passwd, parser, remplir la table des utilisateurs
- [x] **U.4** user_create : créer une entrée, hasher le mot de passe, écrire dans /etc/passwd
- [x] **U.5** Premier boot : si aucun utilisateur, setup initial (demander hostname, créer root + un utilisateur, mots de passe)
- [x] **U.6** Session : utilisateur courant, env USER, HOME, PS1 selon l'utilisateur ; commande whoami

### Fait — login
- [x] **U.7** Login au boot / après exit : shell_login() demande username + mot de passe avant le shell ; vérification contre /etc/passwd ; commande logout pour revenir au login

### Fait — changement d'utilisateur
- [x] **U.8** Commande su (switch user) : demande mot de passe, change current_user et env (USER, HOME, PS1) via user_set_current()

### Fait — groupes
- [x] **U.9** Format /etc/group (groupname:gid:liste_users)
- [x] **U.10** group_load, lecture au boot ; utilisateur peut appartenir à plusieurs groupes
- [x] **U.11** Commande groups (afficher les groupes de l’utilisateur courant)

### Fait — permissions et droits
- [x] **U.12** Vérification des droits : avant chaque accès fichier (read, write, delete), vérifier uid/gid/mode de l’inode (owner, group, other)
- [x] **U.13** Commande chown (changer propriétaire d’un fichier) ; réservé root ou propriétaire
- [x] **U.14** Commande chgrp (changer groupe d’un fichier)
- [x] **U.15** Commande chmod (changer les permissions rwx d’un fichier)

---

## PHASE 11 : Configuration et persistance - COMPLÉTÉ

- [x] **C.1** Structure de config (clavier, date/heure, timezone, format 24h, etc.)
- [x] **C.2** config_load / config_save depuis un fichier (ex. /etc/config)
- [x] **C.3** Hostname : get/set, sauvegarder sur disque (ex. /etc/hostname ou fichier dédié)
- [x] **C.4** Variables d'environnement : env_get, env_set, env_list ; expansion dans le prompt (\w = répertoire courant)
- [x] **C.5** Commandes export, env
- [x] **C.6** Historique shell : sauvegarder dans un fichier, recharger au démarrage
- [x] **C.7** timedatectl (afficher / configurer date et heure)

---

## PHASE 12 : Build et déploiement - COMPLÉTÉ (5/5)

- [x] **M.1** Script de build : compiler la libc, compiler le kernel, lier, produire l'exécutable
- [x] **M.2** Générer l'ISO avec GRUB (multiboot)
- [x] **M.3** Makefile (ou script) : cible run = build + qemu-system-i386 avec CD-ROM + disque + réseau
- [x] **M.4** Image disque (qemu-img) montée en IDE ; le FS persiste entre deux runs
- [x] **M.5** Tests de non-régression : tests user, group, filesystem, FS indirect (fichiers > 32 Ko), libc string/stdlib extra (kernel/arch/i386/test.c)

---

## PHASE 13 : API graphique (framebuffer) - COMPLÉTÉ (5/6, optionnel partiel)

- [x] **G.1** Mode graphique VBE/VESA (1024×768, 32 bpp) ; framebuffer via GRUB2 (bit 12) ou VBE (bit 11) ; validation bpp/dimensions
- [x] **G.2** Module gfx : gfx_init(), gfx_width/height/bpp/pitch/cols/rows, gfx_is_active(), gfx_backbuffer() — header gfx.h
- [x] **G.3** Double buffering : backbuf malloc'd, gfx_flip() (copie complète), gfx_flip_rect() (copie partielle clippée)
- [x] **G.4** Primitives : gfx_put_pixel, gfx_fill_rect, gfx_draw_rect, gfx_draw_line (Bresenham) — couleur 32 bits, clipping
- [x] **G.5** gfx_clear(color) ; police bitmap 8×16 (font8x16.h, 256 glyphes), gfx_draw_char/draw_string (avec fond), gfx_draw_char_nobg/draw_string_nobg (sans fond), gfx_putchar_at
- [ ] **G.6** (Optionnel) Transparence alpha — partiellement fait via draw_char_nobg (pas de blending alpha réel)

---

## PHASE 14 : Interface graphique (HUD / thèmes) - COMPLÉTÉ (6/7, optionnel partiel)

- [x] **I.1** Système de thèmes : palette mono noir (DT_BG, DT_SURFACE, DT_BORDER, DT_TEXT, DT_TEXT_DIM/MED/SUB, DT_TASKBAR_BG, DT_DOCK_PILL, DT_ICON, DT_SEL_BG, DT_FIELD_*, DT_ERROR) — constantes dans desktop.h
- [x] **I.2** Panneaux : rectangles arrondis (draw_rounded_rect, draw_rounded_rect_outline), cercles pleins/anneaux, icônes colorées dessinées en primitives (folder, terminal, globe, pencil, gear, monitor, power)
- [x] **I.3** Layout HUD : dock en bas (48px, pill centrée avec 6 icônes + séparateur), horloge grande (5× scale) + date + infos système en bas à droite, zone centrale pour le terminal
- [x] **I.4** Widget horloge (heure HH:MM en 5× scale, jour de la semaine + mois + jour, infos résolution + RAM) ; tooltip label au-dessus du dock ; tray droit (username + clock + power)
- [x] **I.5** Au boot : splash screen animé (fade-in logo "IMPOS" + spinner ring + fade-out) → setup wizard GUI (si premier boot) → login GUI (avatar + sélection user + mot de passe) → desktop_run() (event loop dock)
- [x] **I.6** Terminal dans la GUI : desktop_open_terminal() crée une window region (terminal_set_window) dans la zone au-dessus du dock, terminal_set_window_bg pour le fond ; desktop_close_terminal() restaure le plein écran
- [ ] **I.7** (Optionnel) Animations — partiellement fait : splash screen a fade-in/fade-out + spinner animé ; pas de slide/fade pour les panneaux

---

## PHASE 15 : Port forwarding et services réseau - À DÉMARRER

- [ ] **P.1** Implémenter TCP (connexions entrantes/sortantes, état des connexions) ; optionnellement UDP
- [ ] **P.2** Petit serveur TCP (ex. port 80 ou 9000) qui répond (page HTML ou JSON)
- [ ] **P.3** Documenter et utiliser hostfwd de QEMU (ex. hostfwd=tcp:8080-:80) ; tester depuis l'hôte (navigateur ou curl)
- [ ] **P.4** (Optionnel) Reverse forwarding, firewall minimal

---

## PHASE 16 : Accès aux applications Windows - À DÉMARRER

### Option A — Virtualisation
- [ ] **W.A.1** Documenter (ou intégrer) comment lancer une VM Windows (ex. QEMU) depuis ou à côté d'ImposOS
- [ ] **W.A.2** Réseau entre la VM et ImposOS (NAT, port forwarding) pour que les deux communiquent
- [ ] **W.A.3** Doc utilisateur : comment utiliser les .exe via la VM + port forwarding

### Option B — Couche type Wine (long terme)
- [ ] **W.B.1** Chargeur PE : parser un .exe, charger en mémoire, résoudre les imports DLL
- [ ] **W.B.2** Stubs ntdll / kernel32 minimal (fichiers, mémoire) qui appellent ImposOS
- [ ] **W.B.3** Faire tourner un .exe console très simple
- [ ] **W.B.4** (Optionnel) user32 / gdi32 minimal pour des fenêtres

---

## PHASE 17 : OS « outil fonctionnel » - À DÉMARRER

- [ ] **O.1** Build unique (ex. ./build.sh), README avec prérequis (toolchain, QEMU)
- [ ] **O.2** Séquence de boot propre (message ou logo ImposOS, pas de debug en prod)
- [ ] **O.3** Messages d'erreur en français ; logs optionnels
- [ ] **O.4** Doc utilisateur : make run, port forwarding, limites ; lien vers ce ROADMAP
- [ ] **O.5** Vérifier et documenter la persistance (disque + config)
- [ ] **O.6** Scénario démo reproductible (boot → shell ou GUI → appel d'un service depuis l'hôte)

---

## Structure des fichiers actuels

```
impos/
├── Makefile                 Cibles all, build, iso, run, run-disk, clean, etc.
├── build.sh                  Build libc + kernel, install headers
├── config.sh                 Configuration (PROJECTS, toolchain)
├── clean.sh
├── iso.sh
├── setup.sh
├── qemu.sh
├── default-host.sh
├── headers.sh
├── target-triplet-to-arch.sh
├── ROADMAP.md                Ce document
│
├── kernel/
│   ├── Makefile
│   ├── arch/
│   │   └── i386/
│   │       ├── boot.S        Multiboot + _start → kernel_main
│   │       ├── crti.S, crtn.S
│   │       ├── linker.ld     Sections à 1 MiB
│   │       ├── make.config   KERNEL_ARCH_OBJS (21 objets)
│   │       ├── tty.c         VGA 80×25 + mode graphique (windowed TTY)
│   │       ├── vga.h         Couleurs, vga_entry_color
│   │       ├── ata.c         Driver ATA
│   │       ├── fs.c          Système de fichiers (inodes, blocs indirects, symlinks)
│   │       ├── shell.c       Boucle + 36 commandes
│   │       ├── vi.c          Éditeur modal (VGA texte + graphique)
│   │       ├── config.c      Config /etc/config
│   │       ├── env.c         Variables d'environnement
│   │       ├── user.c        Utilisateurs, /etc/passwd
│   │       ├── group.c       Groupes, /etc/group
│   │       ├── hash.c        Hachage mots de passe
│   │       ├── hostname.c    Hostname
│   │       ├── acpi.c        ACPI (RSDP, tables, shutdown S5)
│   │       ├── gfx.c         API graphique (framebuffer VBE 32bpp, double buffering)
│   │       ├── font8x16.h    Police bitmap 256 glyphes
│   │       ├── desktop.c     Desktop environment (splash, login, dock, terminal)
│   │       ├── test.c        Tests de régression (~107 tests)
│   │       ├── pci.c         Énumération PCI
│   │       ├── rtl8139.c     Driver Ethernet RTL8139
│   │       ├── net.c         Ethernet, config réseau
│   │       ├── arp.c         ARP (requête/réponse, cache)
│   │       └── ip.c          IP, ICMP (ping)
│   ├── include/
│   │   └── kernel/          Headers (tty, vga, ata, fs, shell, config, env, user, group, hash,
│   │                          hostname, acpi, gfx, desktop, pci, rtl8139, net, arp, ip, vi, multiboot)
│   └── kernel/
│       └── kernel.c          kernel_main, boucle shell/desktop, prompt PS1
│
└── libc/
    ├── Makefile
    ├── include/
    │   ├── stdio.h, stdlib.h, string.h, setjmp.h
    │   └── sys/cdefs.h
    ├── stdio/                getchar, putchar, printf, puts, snprintf
    ├── stdlib/               exit, abort, malloc, atoi, atol, rand, qsort, bsearch, abs
    ├── string/               memcpy, memset, memmove, memcmp, memchr, strlen, strnlen,
    │                          strcmp, strncmp, strcpy, strncpy, strcat, strchr, strrchr,
    │                          strstr, strtok, strdup, strndup, strcspn, strspn, strpbrk
    └── setjmp/               setjmp.S
```

---

## Statistiques du code

| Élément | Nombre |
|---------|--------|
| **Fichiers C kernel (arch i386)** | 21 (.c) + 3 (.S) + 1 (.ld) + 1 (.h font) |
| **Headers kernel** | ~20 (tty, vga, ata, fs, shell, config, env, user, group, hash, hostname, acpi, gfx, desktop, pci, rtl8139, net, arp, ip, vi, multiboot) |
| **Fichiers libc** | ~25+ (stdio, stdlib, string, setjmp) |
| **Commandes shell** | 36 (dont chmod, chown, useradd, userdel, logout, shutdown, gfxdemo, id, ln, readlink) |
| **Phases 1–2, 4–5, 7–8, 11–12** | Complétées (Boot, VGA, Clavier, Disque, Shell, vi, Config, Build + tests) |
| **Phase 3** | ~24 fait, ~1 optionnel (L.10) |
| **Phase 6** | 16 fait, 1 optionnel (F.17 quotas) |
| **Phase 9** | 12 fait, 7 à faire (N.13–N.19 : IRQ, UDP, TCP, sockets) |
| **Phase 10** | 17 fait — complet |
| **Phase 13** | **5 fait**, 1 optionnel (G.6 alpha) — **COMPLÉTÉ** |
| **Phase 14** | **6 fait**, 1 optionnel (I.7 animations) — **COMPLÉTÉ** |
| **Phases 15–17** | 0 fait, 17 à faire (Port forwarding, Windows, Outil) |

---

## Outils de développement

- **Toolchain** : cross-compiler i386 (ex. i686-elf-gcc ou équivalent), configuré via config.sh.
- **Émulateur** : QEMU (qemu-system-i386) avec CD-ROM (ISO), disque IDE (impos_disk.img), RTL8139 (réseau user mode), 128 Mo RAM.
- **Boot** : GRUB (menuentry multiboot) dans l'ISO.

---

## Ordre recommandé (visualisation complète)

Les phases graphiques (13–14) sont complétées. Il reste ~27 étapes :

**Ordre recommandé :** Phase 9 (TCP) → Phase 15 (port forwarding) → Phase 17 (outil fonctionnel) → Phase 16 (Windows)

---

**Dernière mise à jour** : Février 2026 (API graphique + desktop environment complets, ACPI shutdown, 36 commandes shell, login GUI, splash screen, dock avec icônes colorées)
**Next step immédiat** : Phase 9 (TCP) ou Phase 17 (outil fonctionnel)
**Phase actuelle** : Phase 14 complétée (Desktop environment)
**État global** : ~123 / ~150 étapes → fondations + graphique + GUI complets, reste ~27 tâches (TCP + port forwarding + Windows + outil)
