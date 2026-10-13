#ifndef VIDEO_TERRAINCACHE_DOS_H
#define VIDEO_TERRAINCACHE_DOS_H

#include <stdbool.h>
#include <stdint.h>

/* MS-DOS: the viewport tile pass draws into a persistent terrain layer
 * and only redraws cells whose contents changed; the layer is copied
 * into the frame before units are drawn.
 *
 *   if (VideoDOS_TerrainBegin(...)) {
 *       for each cell: if (VideoDOS_TerrainCell(...)) draw the cell;
 *       VideoDOS_TerrainEnd();
 *   }
 */
typedef struct TerrainKey {
	uint16_t ground, overlay, fog, debris;
	uint8_t house, fogOverlay;
} TerrainKey;

extern bool VideoDOS_TerrainBegin(int x0, int y0, int viewportX1, int viewportY1, int mode);
extern bool VideoDOS_TerrainCell(uint16_t packed, int left, int top, const TerrainKey *key);
extern void VideoDOS_TerrainEnd(void);

#endif
