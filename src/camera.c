#include "common.h"

#include "dma.h"
#include "entity.h"
#include "joy.h"
#include "player.h"
#include "stage.h"
#include "tables.h"
#include "tools.h"
#include "vdp.h"

#include "camera.h"

// Since only one row or column of tiles is drawn at a time
#define CAMERA_MAX_SPEED 	0xFFF
#define FOCUS_SPEED 		5
// While following the player focus on a variable point left/right of the player
// The idea is to move slowly along this line relative to the player, but not globally
// limit the speed that way or we will fall behind
#define PAN_SPEED	0x200
#define PAN_SIZE	0x6000
// When cameraShake is nonzero the camera will shake, and decrement this value
// each frame until it becomes zero again
uint16_t cameraShake;
// Tile attr buffer to draw offscreen during map scroll
uint16_t mapbuf[64];
// Alternates between drawing row and column each frame when moving diagonally
uint8_t diag_tick;

void camera_init() {
	camera.target = &player;
	camera.x = camera.x_mark = pixel_to_sub(SCREEN_HALF_W);
	camera.y = camera.y_mark = pixel_to_sub(SCREEN_HALF_H + 8);
	camera.x_shifted = 0;
	camera.y_shifted = 8;
	camera.x_offset = 0;
	cameraShake = 0;
}

void camera_set_position(int32_t x, int32_t y) {
	// Don't let the camera leave the stage
	if(x > block_to_sub(stageWidth) - pixel_to_sub(SCREEN_HALF_W))
		x = block_to_sub(stageWidth) - pixel_to_sub(SCREEN_HALF_W);
	if(y > block_to_sub(stageHeight) - pixel_to_sub(SCREEN_HALF_H+2))
		y = block_to_sub(stageHeight) - pixel_to_sub(SCREEN_HALF_H+2);
	if(x < pixel_to_sub(SCREEN_HALF_W)) x = pixel_to_sub(SCREEN_HALF_W);
	if(y < pixel_to_sub(SCREEN_HALF_H+2)) y = pixel_to_sub(SCREEN_HALF_H+2);
	// Apply
	camera.x = camera.x_mark = x;
	camera.y = camera.y_mark = y;
	camera.x_shifted = (x >> CSF) - SCREEN_HALF_W;
	camera.y_shifted = (y >> CSF) - SCREEN_HALF_H;
	// Update quick fetch cutoff values
	camera_xmin = camera.x - pixel_to_sub(SCREEN_HALF_W + 32);
	camera_xsize = pixel_to_sub(SCREEN_WIDTH + 64);
	camera_ymin = camera.y - pixel_to_sub(SCREEN_HALF_H + 32);
	camera_ysize = pixel_to_sub(SCREEN_HEIGHT + 64);
}

void camera_shake(uint16_t time) {
	cameraShake = time;
}

void camera_update() {
	PF_BGCOLOR(0x08E);
	int32_t x_next, y_next;
	if(camera.target) {
		// If following the player focus on where they are walking/looking
		if(camera.target == &player) {
			if(player.dir == LEFT) {
				//camera.x_offset = -pixel_to_sub(48);
				camera.x_offset -= PAN_SPEED;
				if(camera.x_offset < -PAN_SIZE) camera.x_offset = -PAN_SIZE;
			} else {
				//camera.x_offset = pixel_to_sub(48);
				camera.x_offset += PAN_SPEED;
				if(camera.x_offset > PAN_SIZE) camera.x_offset = PAN_SIZE;
			}
			if(!controlsLocked && joy_down(BUTTON_UP) && player.y_speed <= 0) {
				camera.y_offset -= PAN_SPEED;
				if(camera.y_offset < -PAN_SIZE) camera.y_offset = -PAN_SIZE;
			} else {
				camera.y_offset += PAN_SPEED;
				if(camera.y_offset > 0) camera.y_offset = 0;
			}
			// Calculate where camera will be next frame
			x_next = camera.x +
					(((floor(camera.target->x) + camera.x_offset) - camera.x) >> 4);
			y_next = camera.y +
					(((floor(camera.target->y) + camera.y_offset) - camera.y) >> 4);
		} else {
			// Just stick to the target object without any offset
			camera.x_offset = 0;
			camera.y_offset = 0;
			x_next = camera.x +
					(((floor(camera.target->x) + camera.x_offset) - camera.x) >> 5);
			y_next = camera.y +
					(((floor(camera.target->y) + camera.y_offset) - camera.y) >> 5);
		}
	} else { // Camera isn't following anything
		return;
	}
	// Enforce max camera speed
	if(x_next - camera.x < -CAMERA_MAX_SPEED) x_next = camera.x - CAMERA_MAX_SPEED;
	if(y_next - camera.y < -CAMERA_MAX_SPEED) y_next = camera.y - CAMERA_MAX_SPEED;
	if(x_next - camera.x > CAMERA_MAX_SPEED) x_next = camera.x + CAMERA_MAX_SPEED;
	if(y_next - camera.y > CAMERA_MAX_SPEED) y_next = camera.y + CAMERA_MAX_SPEED;
	// Don't let the camera leave the stage
	if(stageID == 18) { // Special case for shelter
		x_next = pixel_to_sub(SCREEN_HALF_W + 8);
		y_next = pixel_to_sub(SCREEN_HALF_H + 16);
	} else {
		if(x_next < pixel_to_sub(SCREEN_HALF_W + 2)) {
			x_next = pixel_to_sub(SCREEN_HALF_W + 2);
		} else if(x_next > block_to_sub(stageWidth) - pixel_to_sub(SCREEN_HALF_W + 2)) {
			x_next = block_to_sub(stageWidth) - pixel_to_sub(SCREEN_HALF_W + 2);
		}
		if(y_next < pixel_to_sub(SCREEN_HALF_H + 2)) {
			y_next = pixel_to_sub(SCREEN_HALF_H + 2);
		} else if(y_next > block_to_sub(stageHeight) - pixel_to_sub(SCREEN_HALF_H + 2)) {
			y_next = block_to_sub(stageHeight) - pixel_to_sub(SCREEN_HALF_H + 2);
		}
	}
	// Camera shaking
	// stay within the same (pre-shake) 8x8 area to avoid pointless map redraw
	if(cameraShake && (--cameraShake & 1)) {
		int16_t x_shake = (random() & 0x7FF) - 0x400;
		int16_t y_shake = (random() & 0x7FF) - 0x400;
		if((x_next & 0xF000) == ((x_next + x_shake) & 0xF000)) x_next += x_shake;
		if((y_next & 0xF000) == ((y_next + y_shake) & 0xF000)) y_next += y_shake;
	}
	// Shifted values
	camera.x_shifted = (x_next >> CSF) - SCREEN_HALF_W;
	camera.y_shifted = (y_next >> CSF) - SCREEN_HALF_H;
	// Update quick fetch cutoff values
	camera_xmin = camera.x - pixel_to_sub(SCREEN_HALF_W + 32);
	camera_xsize = pixel_to_sub(SCREEN_WIDTH + 64);
	camera_ymin = camera.y - pixel_to_sub(SCREEN_HALF_H + 32);
	camera_ysize = pixel_to_sub(SCREEN_HEIGHT + 64);
	// Morph the stage if the camera is moving
	morphingColumn = sub_to_tile(x_next) - sub_to_tile(camera.x);
	morphingRow = sub_to_tile(y_next) - sub_to_tile(camera.y);
	if(morphingColumn | morphingRow) {
		const uint8_t *pxa = tileset_info[stageTileset].PXA;
		// Queue row and/or column mapping
		if(morphingColumn) {
			// Draw row OR column, and when going diagonally, alternate
			if(morphingRow && (++diag_tick & 1)) {
				morphingColumn = 0;
				x_next = camera.x;
			} else {
				int16_t x = sub_to_tile(x_next) + (morphingColumn == 1 ? 30 : -30);
				int16_t y = sub_to_tile(y_next) - 16 /*+ morphingRow*/;
				if(x >= 0 && x < (int16_t)(stageWidth) << 1) {
					for(uint16_t i = 32; i--; ) {
						// It's actually faster to just draw garbage than have these checks
						//if(y >= stageHeight << 1) break;
						//if(y >= 0) {
							// Fuck math tbh
							uint16_t b = stage_get_block(x>>1, y>>1);
							uint16_t t = ((b&15) << 1) + ((b>>4) << 6);
							uint16_t ta = pxa[b]; //stage_get_block_type(x>>1, y>>1);
							uint16_t pal = (ta == 0x43 || ta & 0x80) ? PAL1 : PAL2;
							mapbuf[y&31] = TILE_ATTR(pal, (ta&0x40) > 0, 
									0, 0, TILE_TSINDEX + t + (x&1) + ((y&1)<<5));
						//}
						y++;
					}
					DMA_queueDma(DMA_VRAM, (uint32_t) mapbuf, VDP_PLAN_A + ((x & 63) << 1), 32, 128);
				}
			}
		}
		if(morphingRow) {
			if(morphingColumn && !(diag_tick & 1)) {
				morphingRow = 0;
				y_next = camera.y;
			} else {
				int16_t y = sub_to_tile(y_next) + (morphingRow == 1 ? 15 : -15);
				int16_t x = sub_to_tile(x_next) - 32 /*+ morphingColumn*/;
				if(y >= 0 && y < (int16_t)(stageHeight) << 1) {
					for(uint16_t i = 64; i--; ) {
						//if(x >= stageWidth << 1) break;
						//if(x >= 0) {
							uint16_t b = stage_get_block(x>>1, y>>1);
							uint16_t t = ((b&15) << 1) + ((b>>4) << 6);
							uint16_t ta = pxa[b]; //stage_get_block_type(x>>1, y>>1);
							uint16_t pal = (ta == 0x43 || ta & 0x80) ? PAL1 : PAL2;
							mapbuf[x&63] = TILE_ATTR(pal, (ta&0x40) > 0, 
									0, 0, TILE_TSINDEX + t + (x&1) + ((y&1)<<5));
						//}
						x++;
					}
					DMA_queueDma(DMA_VRAM, (uint32_t) mapbuf, VDP_PLAN_A + ((y & 31) << 7), 64, 2);
				}
			}
		}
	}
	if(!morphingColumn && (abs(camera.x_mark - x_next) > 0x1FFF || abs(camera.y_mark - y_next) > 0x1FFF)) {
		camera.x_mark = x_next;
		camera.y_mark = y_next;
		entities_update_inactive();
	}
	// Apply camera position
	camera.x = x_next;
	camera.y = y_next;

	PF_BGCOLOR(0x000);
}
