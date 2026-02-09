# ImposOS - Progression du développement

## Prochaines étapes (ce qu'il faut faire maintenant)

### Option 1 : Finitions et déploiement (recommandé)
1. **Phase 17** — OS « outil fonctionnel » (README, boot propre, doc, démo)
2. **Phase 16** — Accès Windows (VM ou Wine) — long terme

### Option 2 : Améliorations optionnelles
1. **G.6** — Transparence alpha (API graphique)
2. **I.7** — Animations (fade, slide) pour panneaux GUI
3. **P.4** — Reverse forwarding, firewall minimal

---

## État actuel (vue rapide)

| Ce qui marche | Ce qui manque |
|---------------|---------------|
| ✅ Boot i386, VGA 80×25 + **mode graphique VBE (1024×768, 32bpp)** | ❌ Windows (VM ou Wine) |
| ✅ **Desktop environment** : splash, login GUI, dock, horloge, terminal en GUI | ❌ README, doc, boot propre (Phase 17) |
| ✅ Shell avec **41 commandes** | |
| ✅ FS complet (permissions, symlinks, blocs indirects, quotas) | |
| ✅ **Stack réseau complète** : L2 (RTL8139 + PCnet-FAST III) → L3 (IP/ARP/ICMP) → L4 (UDP/TCP) → Socket API | |
| ✅ **Apps réseau** : DHCP, DNS, ping, HTTP/1.0 serveur | |
| ✅ libc complète (fprintf, fscanf, sscanf, FILE API, rand, qsort, bsearch) | |
| ✅ Users + groupes, login (GUI ou texte), su, quotas | |
| ✅ **ACPI** : shutdown propre via S5 sleep state | |

**Étapes : ~143 faites / ~150 total → ~7 restantes (optionnel + Windows + outil)**

---

## Résumé exécutif (détaillé)

| Indicateur | Phase | État | Détails |
|------------|-------|------|---------|
| **Étapes faites / total** | — | ~143 / ~150 | Fondations + graphique + GUI + réseau complet ; ~7 étapes restantes (Phases 16–17 + optionnels) |
| **Phase actuelle** | 15 | Services réseau | Stack réseau complète avec HTTP server |
| **Boot / Noyau** | 1 | 6/6 | Multiboot, linker, crt, kernel_main, ISO, QEMU |
| **Affichage (VGA)** | 2 | 6/6 | VGA 80×25, TTY, scroll, curseur, couleurs |
| **libc** | 3 | **complet** | string, printf, putchar, getchar, setjmp, exit, malloc/free/calloc/realloc, sprintf, **fprintf, fscanf, sscanf, FILE API (fopen/fclose/fread/fwrite/fgetc/fputc/fgets/fputs)**, atoi/atol/atoll, strtol, strstr, strdup/strndup, strrchr, strnlen, memchr, strcspn, strspn, strpbrk, rand/srand, qsort, bsearch, abs/div/ldiv, stdarg. |
| **Clavier** | 4 | 3/3 | Scan codes, getchar, AZERTY/QWERTY (setlayout) |
| **Disque (ATA)** | 5 | 3/3 | ATA read/write/flush, détection |
| **Système de fichiers** | 6 | **complet** | format, load/sync, CRUD, chemins, persistance, uid/gid/mode, chmod/chown, symlinks, droits, blocs indirects, validation, **quotas par utilisateur**. |
| **Shell** | 7 | 10/10 | Boucle, **41 commandes**, historique, Tab, PS1 |
| **Éditeur (vi)** | 8 | 3/3 | vi modal (ouvrir, éditer, sauver) — VGA texte et mode graphique |
| **Réseau** | 9 | **complet** | **L2 :** RTL8139 + PCnet-FAST III (auto-détection). **L3 :** IP, ARP (cache), ICMP (ping). **L4 :** UDP (8 bindings, ring buffer), TCP (8 connexions, 4KB buffers, machine à états). **Apps :** Socket API (16 sockets), DNS client, DHCP DORA, HTTP/1.0 serveur. **Commandes :** ifconfig, ping, lspci, arp, nslookup, dhcp, connect, httpd. |
| **Utilisateurs** | 10 | complet | /etc/passwd, /etc/group, hash, setup, session, whoami, useradd, userdel, groupes, chmod, chown, droits, login (texte + GUI), logout, su. |
| **Config** | 11 | 7/7 | /etc/config, hostname, env, historique, timedatectl |
| **Build** | 12 | 5/5 | build.sh, ISO, make run, disque, tests de régression |
| **API graphique** | 13 | **5/6** | VBE 32bpp, double buffering, primitives 2D, police bitmap. G.6 optionnel (alpha). |
| **Interface GUI** | 14 | **6/7** | Thème mono noir, dock, horloge, splash, login GUI, terminal en GUI. I.7 optionnel (animations). |
| **Services réseau** | 15 | **3/4** | TCP ✓, HTTP serveur ✓, hostfwd QEMU ✓. P.4 optionnel (firewall). |
| **Windows (VM/Wine)** | 16 | 0/7 | Option A (VM) ou B (Wine) — **à faire** |
| **Outil fonctionnel** | 17 | 0/6 | README, boot propre, messages FR, doc, démo — **à faire** |

### Ce qui reste à faire (vue d'ensemble)

| Bloc | Phase | Étapes restantes | Ordre de grandeur |
|------|-------|-----------------|-------------------|
| **API graphique (G.6)** | 13 | 1 tâche optionnelle : transparence alpha | optionnel |
| **Interface graphique (I.7)** | 14 | 1 tâche optionnelle : animations (fade, slide) | optionnel |
| **Services réseau (P.4)** | 15 | 1 tâche optionnelle : reverse forwarding, firewall | optionnel |
| **Windows : VM (W.A)** | 16 | 3 étapes | 2–4 sem |
| **Windows : Wine (W.B)** | 16 | 4 étapes | plusieurs mois à années |
| **Outil fonctionnel (O)** | 17 | 6 étapes | 1–2 sem |

En résumé : **tout le coeur de l'OS est complet.** Shell 41 commandes, FS complet (permissions/symlinks/blocs indirects/quotas), **stack réseau complète** (L2→L4 + apps), users + groupes + login GUI/texte + su, libc complète (avec FILE API), API graphique VBE 32bpp, desktop environment, ACPI shutdown, ~190 tests de régression. Il reste les finitions (doc, boot propre) et Windows.

### Bloqueurs critiques

Aucun bloqueur critique. Toutes les phases techniques sont complétées.

### Ce qui fonctionne (état actuel)

- Boot i386 (Multiboot) et chargement du kernel à 1 MiB.
- Affichage mode texte VGA 80×25 avec couleurs, scroll, curseur.
- **Mode graphique VBE/VESA** : framebuffer 32bpp (1024×768 par défaut), double buffering, primitives 2D, police bitmap 8×16.
- **Desktop environment** : splash screen animé, login GUI, setup wizard, dock avec 7 icônes, horloge, terminal en GUI, thème mono noir.
- **libc complète** : string, stdio (printf, fprintf, fscanf, sscanf, fopen/fclose/fread/fwrite/fgetc/fputc/fgets/fputs), stdlib (malloc/free/calloc/realloc, atoi/atol/atoll, strtol, rand/srand, qsort, bsearch, abs/div/ldiv), setjmp, stdarg.
- Clavier (polling), getchar, deux layouts (AZERTY/QWERTY) via `setlayout`.
- Driver ATA : lecture/écriture secteurs, flush, détection disque.
- **Système de fichiers complet** : superbloc, inodes, répertoires, chemins, persistance, uid/gid/mode, chmod/chown, symlinks, blocs indirects, validation au load, quotas par utilisateur.
- Shell avec **41 commandes** : help, man, echo, cat, ls, cd, pwd, touch, mkdir, rm, clear, history, vi, sync, exit, logout, shutdown, timedatectl, ifconfig, ping, lspci, arp, export, env, whoami, chmod, chown, useradd, userdel, setlayout, su, id, ln, readlink, test, gfxdemo, connect, nslookup, dhcp, httpd, quota.
- Éditeur de ligne : backspace, Ctrl+U, historique, complétion Tab, prompt PS1 avec \w et couleurs.
- Éditeur vi modal : ouvrir fichier, éditer, sauvegarder — VGA texte et mode graphique.
- **ACPI** : détection RSDP, parsing tables, shutdown propre via S5 sleep state.
- **Stack réseau complète** :
  - **L2** : RTL8139 (QEMU) et PCnet-FAST III (VirtualBox) avec auto-détection. Polling.
  - **L3** : IP (envoi/réception, checksum), ARP (requête/réponse, cache 16 entrées), ICMP (echo request/reply).
  - **L4** : UDP (8 bindings, ring buffer, checksum), TCP (8 connexions, 4KB buffers, machine à états SYN/ESTABLISHED/FIN).
  - **Apps** : Socket API (16 sockets, SOCK_STREAM/SOCK_DGRAM), DNS client (type A via UDP), DHCP DORA (IP/netmask/gateway auto), HTTP/1.0 serveur (port 80).
  - **Commandes** : ifconfig, ping, lspci, arp, nslookup, dhcp, connect, httpd start/stop.
- **Utilisateurs et groupes** : /etc/passwd, /etc/group, hash, useradd, userdel, setup initial, session, login (GUI + texte), logout, su.
- **Quotas** : limites inodes/blocs par utilisateur (/etc/quota).
- Config : /etc/config, hostname, variables d'environnement, historique persistant, timedatectl.
- Build : ISO GRUB, make run (QEMU/VirtualBox), tests de régression (~190 tests).

### Vue détaillée des tâches restantes

**Prochaine étape recommandée :**
- Phase 17 (outil fonctionnel) → Phase 16 (Windows)

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

## PHASE 3 : libc - COMPLÉTÉ

- [x] **L.1** string (basique) : memcpy, memset, memmove, memcmp, strlen, strcmp, strcpy, strncpy, strcat, strncmp, strchr, strtok
- [x] **L.2** putchar (→ TTY), getchar (lecture clavier)
- [x] **L.3** printf (format %s %d %x %c), snprintf, puts
- [x] **L.4** setjmp / longjmp (asm)
- [x] **L.5** exit, abort (dans le kernel : longjmp vers le shell ou boucle)
- [x] **L.6** malloc (allocation bloc libre)
- [x] **L.7** free (libération)
- [x] **L.8** realloc, calloc
- [x] **L.9** sprintf
- [x] **L.10** fprintf, fscanf, sscanf, FILE API (fopen, fclose, fread, fwrite, fgetc, fputc, fgets, fputs, fflush, feof, ferror)
- [x] **L.11** atoi, atol, atoll
- [x] **L.12** strtol
- [x] **L.13** strstr (recherche sous-chaîne)
- [x] **L.14** strrchr
- [x] **L.15** strdup, strndup
- [x] **L.16** strnlen
- [x] **L.17** memchr
- [x] **L.18** strcspn, strspn, strpbrk
- [x] **L.19** stdarg complet via GCC builtins
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

## PHASE 6 : Système de fichiers - COMPLÉTÉ

- [x] **F.1** Définir le format : superbloc (magic, nb inodes, nb blocs), bitmaps, zone inodes, zone données
- [x] **F.2** alloc_inode, alloc_block, free_inode, free_block
- [x] **F.3** Inodes (type fichier/répertoire, taille, blocs directs) ; répertoires = liste (nom, inode), . et ..
- [x] **F.4** fs_initialize : si disque présent, fs_load (lire superbloc, inodes, bitmaps)
- [x] **F.5** Parser les chemins (absolus, relatifs, . et ..)
- [x] **F.6** fs_create_file (fichier ou répertoire), fs_write_file, fs_read_file
- [x] **F.7** fs_delete_file, fs_list_directory, fs_change_directory, fs_get_cwd
- [x] **F.8** fs_sync : écrire superbloc, inodes et blocs modifiés sur le disque
- [x] **F.9** uid, gid dans la structure inode
- [x] **F.10** mode / permissions (bits rwx) dans l'inode
- [x] **F.11** fs_chmod : changer les permissions d'un fichier
- [x] **F.12** fs_chown, fs_chgrp : changer propriétaire et groupe
- [x] **F.13** Vérifier les droits (uid/gid/mode) avant read, write, delete, list
- [x] **F.14** Symlinks : type inode symlink, stocker la cible, résoudre au path lookup
- [x] **F.15** Blocs indirects (single-indirect) pour fichiers > 4 Ko (MAX_FILE_SIZE ~69 Ko)
- [x] **F.16** Validation au load : superbloc, inodes actifs, cwd_inode fallback
- [x] **F.17** Quotas par utilisateur (limites inodes/blocs, /etc/quota)

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

## PHASE 9 : Réseau - COMPLÉTÉ

### Matériel et bas niveau
- [x] **N.1** Énumérer le bus PCI (lire config space, vendor/device ID)
- [x] **N.2** Driver RTL8139 (QEMU) : reset, buffers RX/TX, adresse MAC, polling
- [x] **N.2b** Driver PCnet-FAST III Am79C973 (VirtualBox) : DWIO, SWSTYLE 2, init block, descripteurs RX/TX
- [x] **N.3** Auto-détection NIC (RTL8139 → PCnet fallback), net_send_packet/net_receive_packet

### Protocoles (couches basses)
- [x] **N.4** Frame Ethernet : adresses MAC dest/src, type (0x0806 ARP, 0x0800 IP)
- [x] **N.5** ARP : requête/réponse, cache IP → MAC (16 entrées, timeout 5 min)
- [x] **N.6** IP : en-tête, checksum (one's complement, byte-order correct), démultiplexage par protocole
- [x] **N.7** ICMP : echo request (type 8), echo reply (type 0), checksum
- [x] **N.8** Commande ping IP : envoyer echo request, attendre reply (avec timeout)

### Couche transport
- [x] **N.14** UDP : en-tête, envoi/réception, démultiplexage par port (8 bindings, ring buffer)
- [x] **N.15** TCP : machine à états (LISTEN, SYN_SENT, ESTABLISHED, FIN_WAIT, etc.), handshake 3-way, ACK, fermeture (8 connexions, 4KB buffers)

### Applications réseau
- [x] **N.16** DNS client : résolution nom d'hôte → IP (requête/réponse DNS type A, via UDP port 53)
- [x] **N.17** DHCP client : DORA complet (Discover/Offer/Request/Ack), obtention automatique IP/netmask/gateway
- [x] **N.18** API Socket : 16 sockets, SOCK_STREAM (TCP) et SOCK_DGRAM (UDP)
- [x] **N.19** Serveur HTTP/1.0 : accept connexions entrantes sur port 80, réponse HTML

### Config et commandes
- [x] **N.9** Stocker la config (IP, netmask, gateway, MAC, lien up/down)
- [x] **N.10** Commande ifconfig : afficher/modifier la config réseau
- [x] **N.11** Commande lspci : lister les périphériques PCI
- [x] **N.12** Commande arp : envoyer une requête ARP et afficher la réponse
- [x] **N.20** Commandes connect (DHCP auto), nslookup, dhcp, httpd start/stop

---

## PHASE 10 : Utilisateurs et authentification - COMPLÉTÉ

- [x] **U.1** Format /etc/passwd (ex. username:salt_hex:hash_hex:uid:gid:home)
- [x] **U.2** Fonction de hachage (sel + hash) pour les mots de passe, vérification à la connexion
- [x] **U.3** user_load : lire /etc/passwd, parser, remplir la table des utilisateurs
- [x] **U.4** user_create : créer une entrée, hasher le mot de passe, écrire dans /etc/passwd
- [x] **U.5** Premier boot : si aucun utilisateur, setup initial (hostname, root + utilisateur, mots de passe)
- [x] **U.6** Session : utilisateur courant, env USER, HOME, PS1 ; commande whoami
- [x] **U.7** Login au boot (shell_login ou GUI), logout pour revenir au login
- [x] **U.8** Commande su (switch user)
- [x] **U.9** Format /etc/group (groupname:gid:liste_users)
- [x] **U.10** group_load, lecture au boot ; groupes multiples
- [x] **U.11** Commande groups
- [x] **U.12** Vérification des droits (uid/gid/mode) avant accès fichier
- [x] **U.13** Commande chown (réservé root)
- [x] **U.14** Commande chgrp
- [x] **U.15** Commande chmod

---

## PHASE 11 : Configuration et persistance - COMPLÉTÉ

- [x] **C.1** Structure de config (clavier, date/heure, timezone, format 24h, etc.)
- [x] **C.2** config_load / config_save depuis /etc/config
- [x] **C.3** Hostname : get/set, persistance sur disque
- [x] **C.4** Variables d'environnement : env_get, env_set, env_list
- [x] **C.5** Commandes export, env
- [x] **C.6** Historique shell persistant
- [x] **C.7** timedatectl

---

## PHASE 12 : Build et déploiement - COMPLÉTÉ

- [x] **M.1** Script de build : compiler la libc, compiler le kernel, lier, produire l'exécutable
- [x] **M.2** Générer l'ISO avec GRUB (multiboot)
- [x] **M.3** Makefile : cible run = build + qemu-system-i386 avec CD-ROM + disque + réseau
- [x] **M.4** Image disque montée en IDE ; le FS persiste entre deux runs
- [x] **M.5** Tests de non-régression (~190 tests : string, stdlib, snprintf, sscanf, FS, FS indirect, user, gfx, quota, network)

---

## PHASE 13 : API graphique (framebuffer) - COMPLÉTÉ (5/6, optionnel partiel)

- [x] **G.1** Mode graphique VBE/VESA (1024×768, 32 bpp)
- [x] **G.2** Module gfx : gfx_init(), gfx_width/height/bpp/pitch/cols/rows, gfx_is_active(), gfx_backbuffer()
- [x] **G.3** Double buffering : backbuf, gfx_flip(), gfx_flip_rect()
- [x] **G.4** Primitives : gfx_put_pixel, gfx_fill_rect, gfx_draw_rect, gfx_draw_line (Bresenham)
- [x] **G.5** gfx_clear, police bitmap 8×16, gfx_draw_char/draw_string (avec et sans fond)
- [ ] **G.6** (Optionnel) Transparence alpha

---

## PHASE 14 : Interface graphique (HUD / thèmes) - COMPLÉTÉ (6/7, optionnel partiel)

- [x] **I.1** Système de thèmes : palette mono noir
- [x] **I.2** Panneaux : rectangles arrondis, cercles, icônes colorées
- [x] **I.3** Layout HUD : dock en bas, horloge, zone centrale
- [x] **I.4** Widget horloge, tooltip, tray
- [x] **I.5** Splash screen animé → setup wizard → login GUI → desktop
- [x] **I.6** Terminal dans la GUI : window region
- [ ] **I.7** (Optionnel) Animations (fade, slide pour panneaux)

---

## PHASE 15 : Services réseau - COMPLÉTÉ (3/4)

- [x] **P.1** TCP implémenté (connexions entrantes/sortantes, machine à états)
- [x] **P.2** Serveur HTTP/1.0 (port 80, réponse HTML)
- [x] **P.3** hostfwd QEMU (tcp::8080-:80) ; tester depuis l'hôte (navigateur ou curl)
- [ ] **P.4** (Optionnel) Reverse forwarding, firewall minimal

---

## PHASE 16 : Accès aux applications Windows - À DÉMARRER

### Option A — Virtualisation
- [ ] **W.A.1** Documenter comment lancer une VM Windows depuis ou à côté d'ImposOS
- [ ] **W.A.2** Réseau entre la VM et ImposOS (NAT, port forwarding)
- [ ] **W.A.3** Doc utilisateur : comment utiliser les .exe via la VM

### Option B — Couche type Wine (long terme)
- [ ] **W.B.1** Chargeur PE : parser un .exe, charger en mémoire, résoudre les imports DLL
- [ ] **W.B.2** Stubs ntdll / kernel32 minimal
- [ ] **W.B.3** Faire tourner un .exe console très simple
- [ ] **W.B.4** (Optionnel) user32 / gdi32 minimal pour des fenêtres

---

## PHASE 17 : OS « outil fonctionnel » - À DÉMARRER

- [ ] **O.1** Build unique (ex. ./build.sh), README avec prérequis (toolchain, QEMU)
- [ ] **O.2** Séquence de boot propre (message ou logo ImposOS, pas de debug en prod)
- [ ] **O.3** Messages d'erreur en français ; logs optionnels
- [ ] **O.4** Doc utilisateur : make run, port forwarding, limites
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
│   │       ├── make.config   KERNEL_ARCH_OBJS (32 objets)
│   │       ├── tty.c         VGA 80×25 + mode graphique (windowed TTY)
│   │       ├── vga.h         Couleurs, vga_entry_color
│   │       ├── ata.c         Driver ATA
│   │       ├── fs.c          Système de fichiers (inodes, blocs indirects, symlinks, quotas)
│   │       ├── shell.c       Boucle + 41 commandes
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
│   │       ├── test.c        Tests de régression (~190 tests)
│   │       ├── quota.c       Quotas par utilisateur
│   │       ├── pci.c         Énumération PCI
│   │       ├── rtl8139.c     Driver Ethernet RTL8139 (QEMU)
│   │       ├── pcnet.c       Driver Ethernet PCnet-FAST III (VirtualBox)
│   │       ├── net.c         Couche réseau + auto-détection NIC
│   │       ├── arp.c         ARP (requête/réponse, cache)
│   │       ├── ip.c          IP, ICMP (ping)
│   │       ├── udp.c         UDP (8 bindings, ring buffer)
│   │       ├── tcp.c         TCP (8 connexions, machine à états)
│   │       ├── socket.c      Socket API (16 sockets, SOCK_STREAM/SOCK_DGRAM)
│   │       ├── dns.c         DNS client (type A, UDP)
│   │       ├── dhcp.c        DHCP DORA (Discover/Offer/Request/Ack)
│   │       └── httpd.c       HTTP/1.0 serveur (port 80)
│   ├── include/
│   │   └── kernel/          Headers (~30 : tty, vga, ata, fs, shell, config, env, user, group,
│   │                          hash, hostname, acpi, gfx, desktop, pci, rtl8139, pcnet, net, arp,
│   │                          ip, udp, tcp, socket, dns, dhcp, httpd, io, endian, idt, vi, quota,
│   │                          multiboot, test)
│   └── kernel/
│       └── kernel.c          kernel_main, boucle shell/desktop, prompt PS1
│
└── libc/
    ├── Makefile
    ├── include/
    │   ├── stdio.h, stdlib.h, string.h, setjmp.h
    │   └── sys/cdefs.h
    ├── stdio/                getchar, putchar, printf, puts, snprintf, file, fprintf, fscanf
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
| **Fichiers C kernel (arch i386)** | 32 (.c) + 3 (.S) + 1 (.ld) + 1 (.h font) |
| **Headers kernel** | ~30 |
| **Fichiers libc** | ~28 (stdio, stdlib, string, setjmp) |
| **Commandes shell** | 41 |
| **Tests de régression** | ~190 (string, stdlib, snprintf, sscanf, FS, FS indirect, user, gfx, quota, network) |
| **Phases complétées** | 1–15 (sauf optionnels G.6, I.7, P.4) |
| **Phases à faire** | 16 (Windows), 17 (Outil fonctionnel) |

---

## Outils de développement

- **Toolchain** : cross-compiler i386 (i686-elf-gcc), configuré via config.sh.
- **Émulateurs** :
  - QEMU (qemu-system-i386) avec CD-ROM (ISO), disque IDE, RTL8139, 128 Mo RAM.
  - VirtualBox avec PCnet-FAST III (Am79C973), NAT.
- **Boot** : GRUB (menuentry multiboot) dans l'ISO.

---

## Ordre recommandé

Il reste ~7 étapes (hors optionnels) :

**Ordre recommandé :** Phase 17 (outil fonctionnel) → Phase 16 (Windows)

---

**Dernière mise à jour** : Février 2026 — Stack réseau complète (RTL8139 + PCnet, UDP, TCP, DNS, DHCP, HTTP serveur), 41 commandes shell, quotas, ~190 tests
**Phase actuelle** : Phases 1–15 complétées
**État global** : ~143 / ~150 étapes → coeur OS complet, reste finitions + Windows
