/* How skinc builds the vendored decoders, shared by their units and by
 * media.c, which includes their declarations. */
#ifndef SKINC_MEDIA_CONFIG_H
#define SKINC_MEDIA_CONFIG_H

/* stb_image: PNG and JPEG from memory, no larger a side than the pack
 * loader of decision 0037 takes. */
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_MAX_DIMENSIONS 16384

/* stb_vorbis: whole files in memory, decoded to floats. */
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_INTEGER_CONVERSION

#endif
