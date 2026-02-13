/* Stub <stdbool.h> for ImposOS doom port */
#ifndef _DOOM_STDBOOL_H
#define _DOOM_STDBOOL_H
/* boolean type is defined in doomtype.h as an enum */
/* Avoid redefining true/false if already defined by doomtype.h */
#ifndef __cplusplus
#ifndef bool
#define bool int
#endif
#endif
#endif
