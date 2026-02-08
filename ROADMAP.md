# ImposOS - Progression du développement

## Prochaines étapes (ce qu'il faut faire maintenant)

### Option 1 : Compléter les fondations (recommandé)
Ces étapes rendent le noyau plus solide avant de passer au graphique :

1. **Compléter libc** — realloc, calloc, atol/atoll, strndup, strrchr, stdarg, etc. (Phase 3, tâches L.8–L.22) — quelques semaines
2. **Compléter FS** — blocs indirects, robustesse (Phase 6, tâches F.15–F.17) — quelques semaines
3. **Compléter users** — login au boot, su (Phase 10, tâches U.7–U.8) — quelques semaines
4. **TCP** — implémenter TCP pour le port forwarding (Phase 9, tâches N.13–N.19) — grosse pièce

### Option 2 : Passer directement au graphique
Si tu veux voir un résultat visuel rapidement :

1. **Phase 13** — API graphique (VBE, framebuffer, primitives 2D) — 1–2 mois
3. **Phase 14** — Interface graphique (HUD, thèmes, widgets) — 1–2 mois
4. Revenir aux compléments libc/FS/users/TCP ensuite

---

## État actuel (vue rapide)

| Ce qui marche | Ce qui manque |
|---------------|---------------|
| ✅ Boot i386, VGA 80×25, shell avec 31 commandes | ❌ realloc, calloc, atol/atoll, stdarg complet (libc) |
| ✅ FS (fichiers, répertoires, persistance, permissions, chmod, chown, symlinks) | ❌ blocs indirects, robustesse (FS) |
| ✅ Réseau L2/L3/ICMP (ping fonctionne) | ❌ TCP, UDP, sockets (réseau) |
| ✅ Users + groupes (/etc/passwd, /etc/group, chmod, chown, useradd, userdel) | ❌ login au boot, su (users) |
| ✅ 31 commandes shell, vi, historique, Tab, tests de régression | ❌ API graphique, GUI, port forwarding |

**Étapes : 94 faites / ~150 total → ~56 restantes**

---

## Résumé exécutif (détaillé)

| Indicateur | Phase | État | Détails |
|------------|-------|------|---------|
| **Étapes faites / total** | — | 94 / ~150 | Fondations avancées ; ~56 étapes restantes (Phase 3: 10, Phase 6: 3, Phase 9: 7, Phase 10: 2, Phases 13–17: 30) |
| **Phase actuelle** | 12 | Build (M) | Build complet avec tests de régression (user, group, FS) |
| **Boot / Noyau** | 1 | 6/6 | Multiboot, linker, crt, kernel_main, ISO, QEMU |
| **Affichage (VGA)** | 2 | 6/6 | VGA 80×25, TTY, scroll, curseur, couleurs |
| **libc** | 3 | partiel | **Fait :** string basique, printf, putchar, getchar, setjmp, exit, malloc/free, sprintf, atoi, strtol, strstr, strdup. **Manque :** realloc, calloc, atol/atoll, strndup, strrchr, stdarg complet, etc. |
| **Clavier** | 4 | 3/3 | Scan codes, getchar, AZERTY/QWERTY (setlayout) |
| **Disque (ATA)** | 5 | 3/3 | ATA read/write/flush, détection |
| **Système de fichiers** | 6 | partiel | **Fait :** format, load/sync, CRUD, chemins, persistance, uid/gid/mode sur inodes, chmod/chown, symlinks, vérification des droits. **Manque :** blocs indirects (fichiers plus gros), robustesse erreurs. |
| **Shell** | 7 | 10/10 | Boucle, 31 commandes (dont chmod, chown, useradd, userdel), historique, Tab, PS1 |
| **Éditeur (vi)** | 8 | 3/3 | vi minimal (ouvrir, éditer, sauver) |
| **Réseau** | 9 | partiel | **Fait :** PCI, RTL8139, Ethernet, ARP, IP, ICMP (ping), ifconfig, lspci, arp. **Pas fini :** TCP, UDP, DNS, DHCP, IRQ (tout est en polling), pas de stack « applicative ». Le réseau « fini » pour un OS inclut TCP au minimum. |
| **Utilisateurs** | 10 | partiel | **Fait :** /etc/passwd, /etc/group, hash, setup initial, session, whoami, useradd, userdel, groupes (group_load, membership), chmod, chown, vérification droits fichiers. **Manque :** login au boot, su. |
| **Config** | 11 | 7/7 | /etc/config, hostname, env, historique, timedatectl |
| **Build** | 12 | 5/5 | build.sh, ISO, make run, disque, tests de régression (user, group, FS) |
| **API graphique** | 13 | 0/6 | VBE, framebuffer, double buffer, primitives, polices — **à faire** |
| **Interface GUI** | 14 | 0/7 | Thèmes, panneaux, HUD, widgets, terminal en GUI — **à faire** |
| **Port forwarding** | 15 | 0/4 | TCP, serveur démo, hostfwd QEMU — **à faire** |
| **Windows (VM/Wine)** | 16 | 0/7 | Option A (VM) ou B (Wine) — **à faire** |
| **Outil fonctionnel** | 17 | 0/6 | README, boot propre, messages FR, doc, démo — **à faire** |

### Ce qui reste à faire (vue d’ensemble)

| Bloc | Phase | Étapes / compléments restants | Ordre de grandeur |
|------|-------|------------------------------|-------------------|
| **Compléments libc (L.8–L.22)** | 3 | 10 tâches : realloc, calloc, atol/atoll, strndup, strrchr, stdarg, etc. | semaines |
| **Compléments FS (F.15–F.17)** | 6 | 3 tâches : blocs indirects (gros fichiers), robustesse | semaines |
| **Compléments réseau (N.13–N.19)** | 9 | 7 tâches : IRQ, UDP, TCP, DNS/DHCP optionnel, API socket | TCP = grosse pièce (semaines) |
| **Compléments utilisateurs (U.7–U.8)** | 10 | 2 tâches : login au boot, su | semaines |
| **API graphique (G)** | 13 | 6 étapes | 1–2 mois |
| **Interface graphique (I)** | 14 | 7 étapes | 1–2 mois |
| **TCP + Port forwarding (P)** | 15 | 4 étapes | 2–4 sem (TCP = grosse pièce) |
| **Windows : VM (W.A)** | 16 | 3 étapes | 2–4 sem |
| **Windows : Wine (W.B)** | 16 | 4 étapes | plusieurs mois à années |
| **Outil fonctionnel (O)** | 17 | 6 étapes | 1–2 sem |
| **Autres (multitâche, drivers, etc.)** | — | Tout OS complet en a des dizaines | — |

En résumé : les fondations **avancées** sont en place (shell, FS avec permissions/symlinks, réseau L2/L3/ICMP, users + groupes, libc avec malloc/sprintf/atoi/strtol/strdup/strstr, tests de régression). Il reste les compléments libc (realloc, stdarg, etc.), FS (blocs indirects), utilisateurs (login au boot, su), TCP, puis le bloc graphique, GUI, Windows, outil.

### Bloqueurs critiques

1. Aucun bloquant actuel pour la suite.
2. **TCP manquant** pour le port forwarding : seule la couche ICMP est en place ; TCP est une implémentation conséquente.

### Ce qui fonctionne (état actuel = shell + FS minimal + réseau L2/L3/ICMP + users basique)

- Boot i386 (Multiboot) et chargement du kernel à 1 MiB.
- Affichage mode texte VGA 80×25 avec couleurs, scroll, curseur.
- **libc** : string basique, printf, putchar, getchar, setjmp, exit, malloc/free, sprintf, atoi, strtol, strstr, strdup.
- Clavier (polling), getchar, deux layouts (AZERTY/QWERTY) via `setlayout` et config.
- Driver ATA : lecture/écriture secteurs, flush, détection disque.
- **Système de fichiers** : superbloc, inodes, répertoires, chemins, fs_load/fs_sync, persistance, uid/gid/mode, chmod/chown, symlinks, vérification des droits.
- Shell avec 31 commandes : help, man, echo, cat, ls, cd, pwd, touch, mkdir, rm, clear, history, vi, sync, exit, shutdown, timedatectl, ifconfig, ping, lspci, arp, export, env, whoami, **chmod, chown, useradd, userdel**.
- Éditeur de ligne : backspace, Ctrl+U/etc., historique, complétion Tab, prompt PS1 avec \w et couleurs.
- Éditeur vi minimal : ouvrir fichier, éditer, sauvegarder.
- **Réseau L2/L3/ICMP uniquement** : PCI, RTL8139 (TX/RX), Ethernet, ARP (cache), IP, ICMP (ping). Pas de TCP/UDP.
- **Utilisateurs et groupes** : /etc/passwd, /etc/group (sel + hash), user_create, useradd, userdel, setup initial, session (USER, HOME, PS1), whoami, chmod, chown, vérification droits fichiers. Pas de login au boot, su.
- Config : /etc/config (clavier, date/heure, timezone, 24h), hostname sur disque, variables d'environnement, historique persistant, timedatectl.
- Build : build.sh (libc + kernel), ISO GRUB, make run (QEMU + CD-ROM + disque IDE + RTL8139), **tests de régression (user, group, FS)**.

### Vue détaillée des tâches restantes

**Choix A : Compléter les fondations d'abord**
- Phase 3 compléments libc (L.8–L.22) → Phase 6 compléments FS (F.15–F.17) → Phase 10 compléments users (U.7–U.8 : login, su) → Phase 9 TCP → Phase 13–17 (graphique, GUI, Windows, outil)

**Choix B : Fonctionnalités visibles d'abord**
- Phase 13 (API graphique) → Phase 14 (GUI) → revenir aux compléments → Phase 15–17

**Les deux voies sont valides.** Le choix A donne un OS plus solide avant le graphique. Le choix B donne des résultats visuels plus rapidement.

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

### À faire — mémoire dynamique
- [ ] **L.8** realloc, calloc

### Fait — stdio supplémentaires
- [x] **L.9** sprintf
- [ ] **L.10** fprintf, fscanf, sscanf (si support fichiers/streams)

### Fait — conversion nombres / string
- [x] **L.11** atoi (atol, atoll à faire)
- [x] **L.12** strtol (strtoul, strtoll, strtoull à faire)

### À faire — conversion nombres / string
- [ ] atol, atoll ; strtoul, strtoll, strtoull

### Fait — string supplémentaires
- [x] **L.13** strstr (recherche sous-chaîne)
- [x] **L.15** strdup (strndup à faire)

### À faire — string supplémentaires
- [ ] **L.14** strrchr
- [ ] **L.16** strnlen
- [ ] **L.17** memchr
- [ ] **L.18** strcspn, strspn, strpbrk (optionnel)

### À faire — variadiques
- [ ] **L.19** stdarg propre : va_start, va_arg, va_end, va_list (pour printf/vprintf etc.)

### À faire — stdlib / autres (optionnel)
- [ ] **L.20** rand, srand
- [ ] **L.21** qsort, bsearch
- [ ] **L.22** abs, div, ldiv

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

### À faire — tailles
- [ ] **F.15** Support fichiers plus gros : blocs indirects (ou équivalent) pour dépasser la taille actuelle

### À faire — robustesse
- [ ] **F.16** Gestion d’erreurs robuste : retours d’erreur cohérents (ex. -1 + errno ou codes), messages clairs
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

### À faire — login et changement d’utilisateur
- [ ] **U.7** Login au boot : au démarrage, demander username + mot de passe avant d’entrer dans le shell ; vérifier contre /etc/passwd
- [ ] **U.8** Commande su (switch user) : demander mot de passe (si besoin), changer current_user et env (USER, HOME, PS1) sans redémarrer

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
- [x] **M.5** Tests de non-régression : tests user, group, filesystem (kernel/arch/i386/test.c)

---

## PHASE 13 : API graphique (framebuffer) - À DÉMARRER

- [ ] **G.1** Passer en mode graphique VBE/VESA (résolution fixe, ex. 640×480 ou 800×600, 32 bpp) ; récupérer l'adresse du framebuffer
- [ ] **G.2** Module gfx : init, largeur/hauteur/bpp/pitch exposés au reste du kernel
- [ ] **G.3** Double buffering : buffer en RAM de la taille de l'écran, fonction « flip » qui copie vers le framebuffer
- [ ] **G.4** Primitives : put_pixel, fill_rect, draw_line, draw_rect (couleur 32 bits)
- [ ] **G.5** clear(couleur) ; polices bitmap 8×8 ou 8×16, draw_char, draw_string
- [ ] **G.6** (Optionnel) Transparence alpha ; header public gfx.h

---

## PHASE 14 : Interface graphique (HUD / thèmes) - À DÉMARRER

- [ ] **I.1** Système de thèmes : couleurs (fond, bord, texte, accent), chargement au boot (fichier ou constantes)
- [ ] **I.2** Dessiner des panneaux (rectangle + bordure, optionnel : coins arrondis, semi-transparence)
- [ ] **I.3** Layout type HUD : barre en haut (heure, hostname), barre en bas (infos), zone centrale
- [ ] **I.4** Widget horloge (rafraîchi à la seconde) ; widget système (CPU/RAM/disque si dispo)
- [ ] **I.5** Au boot : lancer le « compositeur » (fond + HUD + panneaux), shell dans une zone dédiée
- [ ] **I.6** Terminal dans la GUI : zone rectangulaire où le TTY est dessiné via gfx (rediriger le TTY)
- [ ] **I.7** (Optionnel) Animations (fade, slide) pour les panneaux

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
│   │       ├── make.config   KERNEL_ARCH_OBJS
│   │       ├── tty.c         VGA 80×25, terminal_*
│   │       ├── vga.h         Couleurs, vga_entry_color
│   │       ├── ata.c         Driver ATA
│   │       ├── fs.c          Système de fichiers
│   │       ├── shell.c       Boucle + 27 commandes
│   │       ├── vi.c          Éditeur minimal
│   │       ├── config.c      Config /etc/config
│   │       ├── env.c         Variables d'environnement
│   │       ├── user.c        Utilisateurs, /etc/passwd
│   │       ├── hash.c        Hachage mots de passe
│   │       ├── hostname.c    Hostname
│   │       ├── pci.c         Énumération PCI
│   │       ├── rtl8139.c     Driver Ethernet
│   │       ├── net.c         Ethernet, config
│   │       ├── arp.c         ARP
│   │       └── ip.c          IP, ICMP
│   ├── include/
│   │   └── kernel/          Headers (tty, vga, ata, fs, shell, config, env, user, hash, hostname, pci, rtl8139, net, arp, ip, vi)
│   └── kernel/
│       └── kernel.c          kernel_main, boucle shell, prompt PS1
│
└── libc/
    ├── Makefile
    ├── include/
    │   ├── stdio.h, stdlib.h, string.h, setjmp.h
    │   └── sys/cdefs.h
    ├── stdio/                getchar, putchar, printf, puts, snprintf
    ├── stdlib/               exit, abort
    ├── string/               memcpy, memset, memmove, memcmp, strlen, strcmp, strcpy, strncpy, strcat, strncmp, strchr, strtok
    └── setjmp/               setjmp.S
```

---

## Statistiques du code

| Élément | Nombre |
|---------|--------|
| **Fichiers C kernel (arch i386)** | 18 (.c) + 3 (.S) + 1 (.ld) |
| **Headers kernel** | 16 |
| **Fichiers libc** | ~25 (stdio, stdlib, string, setjmp) — malloc, sprintf, atoi, strtol, strdup, strstr ajoutés |
| **Commandes shell** | 31 (dont chmod, chown, useradd, userdel) |
| **Phases 1–2, 4–5, 7–8, 11–12** | Complétées (Boot, VGA, Clavier, Disque, Shell, vi, Config, Build + tests) |
| **Phase 3** | 12 fait, 10 à faire (L.8–L.22 : realloc, calloc, atol/atoll, strndup, strrchr, stdarg, etc.) |
| **Phase 6** | 14 fait, 3 à faire (F.15–F.17 : blocs indirects, robustesse) |
| **Phase 9** | 12 fait, 7 à faire (N.13–N.19 : IRQ, UDP, TCP, sockets) |
| **Phase 10** | 15 fait, 2 à faire (U.7–U.8 : login au boot, su) |
| **Phases 13–17** | 0 fait, 30 à faire (API graphique, GUI, Port forwarding, Windows, Outil) |

---

## Outils de développement

- **Toolchain** : cross-compiler i386 (ex. i686-elf-gcc ou équivalent), configuré via config.sh.
- **Émulateur** : QEMU (qemu-system-i386) avec CD-ROM (ISO), disque IDE (impos_disk.img), RTL8139 (réseau user mode), 128 Mo RAM.
- **Boot** : GRUB (menuentry multiboot) dans l'ISO.

---

## Ordre recommandé (visualisation complète)

Les phases ci-dessous représentent encore plusieurs mois de travail. Deux voies possibles :

**Voie A (fondations) :** Phase 3 (libc L.8–L.22) → Phase 6 (FS F.15–F.17) → Phase 10 (U.7–U.8 : login, su) → Phase 9 (TCP) → Phases 13–17

**Voie B (visuel) :** Phase 13 (graphique) → Phase 14 (GUI) → revenir aux compléments → Phases 15–17

---

**Dernière mise à jour** : Février 2026 (commits : libc malloc/sprintf/atoi/strtol/strdup/strstr ; users+groupes, chmod/chown, symlinks, tests)  
**Next step immédiat** : Phase 3 (realloc, stdarg) ou Phase 10 (login au boot, su) ou Phase 13 (API graphique)  
**Phase actuelle** : Phase 12 complétée (Build + tests de régression)  
**État global** : 94 / ~150 étapes → fondations avancées OK, reste ~56 tâches (compléments + graphique + GUI + TCP + Windows + outil)
