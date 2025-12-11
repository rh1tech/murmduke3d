/*
 * Streaming Animation Player for RP2350
 * 
 * Loads ANM files in chunks from SD card instead of loading entire file to memory.
 * Uses PSRAM for frame buffers.
 */

#include "anim_streaming.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../drivers/psram_allocator.h"

// Use Duke3D's file system
extern int32_t TCkopen4load(const char *filename, int readfromGRP);
extern int32_t kread(int32_t handle, void *buffer, int32_t length);
extern int32_t klseek(int32_t handle, int32_t offset, int whence);
extern void kclose(int32_t handle);

//=============================================================================
// ANM File Format Structures (from animlib.h)
//=============================================================================

#pragma pack(push, 1)

typedef struct {
    uint32_t id;                // 4 character ID == "LPF "
    uint16_t maxLps;            // max # largePages allowed (256)
    uint16_t nLps;              // # largePages in this file
    uint32_t nRecords;          // # records (frames) in this file
    uint16_t maxRecsPerLp;      // # records permitted per LP (256)
    uint16_t lpfTableOffset;    // Offset of LP table (1280)
    uint32_t contentType;       // 4 character ID == "ANIM"
    uint16_t width;             // Width in pixels
    uint16_t height;            // Height in pixels
    uint8_t variant;            // 0==ANIM
    uint8_t version;            // Frame rate version
    uint8_t hasLastDelta;       // Has last-to-first delta
    uint8_t lastDeltaValid;     // Last delta is valid
    uint8_t pixelType;          // 0==256 color
    uint8_t compressionType;    // 1==RunSkipDump
    uint8_t otherRecsPerFrm;    // 0 for now
    uint8_t bitmaptype;         // 1==320x200 256-color
    uint8_t recordTypes[32];    // Not implemented
    uint32_t nFrames;           // Actual frame count
    uint16_t framesPerSecond;   // Playback rate
    uint16_t pad2[29];          // Padding to 128 bytes
} lpfileheader_t;

typedef struct {
    uint16_t baseRecord;        // First record in this LP
    uint16_t nRecords;          // Number of records in LP
    uint16_t nBytes;            // Total bytes excluding header
} lp_descriptor_t;

#pragma pack(pop)

//=============================================================================
// Streaming Animation State
//=============================================================================

typedef struct {
    int32_t handle;             // File handle (-1 if not open)
    
    // Header data (read once)
    lpfileheader_t header;
    lp_descriptor_t lpArray[256];
    uint8_t palette[768];
    
    // File position info
    uint32_t dataStartOffset;   // Offset where LP data begins
    
    // Current large page cache
    uint16_t curLpNum;          // Current LP number (0xFFFF = none)
    lp_descriptor_t curLp;      // Current LP descriptor
    
    // Buffers (in PSRAM)
    uint16_t *pageBuffer;       // 32KB buffer for current large page
    uint8_t *imageBuffer;       // 64KB buffer for decoded frame
    
    // Playback state
    int32_t currentFrame;
} anim_stream_t;

static anim_stream_t *animStream = NULL;

//=============================================================================
// RunSkipDump Decompressor
//=============================================================================

static void CPlayRunSkipDump(uint8_t *srcP, uint8_t *dstP)
{
    int8_t cnt;
    uint16_t wordCnt;
    uint8_t pixel;

nextOp:
    cnt = (int8_t)*srcP++;
    if (cnt > 0)
        goto dump;
    if (cnt == 0)
        goto run;
    cnt -= 0x80;
    if (cnt == 0)
        goto longOp;
    // shortSkip
    dstP += cnt;
    goto nextOp;
    
dump:
    do {
        *dstP++ = *srcP++;
    } while (--cnt);
    goto nextOp;
    
run:
    wordCnt = (uint8_t)*srcP++;
    pixel = *srcP++;
    do {
        *dstP++ = pixel;
    } while (--wordCnt);
    goto nextOp;
    
longOp:
    wordCnt = *((uint16_t *)srcP);
    srcP += sizeof(uint16_t);
    if ((int16_t)wordCnt <= 0)
        goto notLongSkip;
    // longSkip
    dstP += wordCnt;
    goto nextOp;

notLongSkip:
    if (wordCnt == 0)
        goto stop;
    wordCnt -= 0x8000;
    if (wordCnt >= 0x4000)
        goto longRun;
    // longDump
    do {
        *dstP++ = *srcP++;
    } while (--wordCnt);
    goto nextOp;

longRun:
    wordCnt -= 0x4000;
    pixel = *srcP++;
    do {
        *dstP++ = pixel;
    } while (--wordCnt);
    goto nextOp;

stop:
    ;
}

//=============================================================================
// Helper Functions
//=============================================================================

// Find which large page contains a given frame
static uint16_t findPage(uint16_t frameNumber)
{
    if (!animStream) return 0;
    
    for (uint16_t i = 0; i < animStream->header.nLps; i++) {
        if (animStream->lpArray[i].baseRecord <= frameNumber &&
            animStream->lpArray[i].baseRecord + animStream->lpArray[i].nRecords > frameNumber) {
            return i;
        }
    }
    return 0;
}

// Load a large page from file into pageBuffer
static bool loadPage(uint16_t pageNumber)
{
    if (!animStream || animStream->handle < 0) return false;
    if (animStream->curLpNum == pageNumber) return true;  // Already loaded
    
    printf("loadPage(%d)\n", pageNumber);
    
    // Large pages are at fixed 64KB (0x10000) offsets after the header area
    // Header area = 0xb00 (2816 bytes) = header (128) + palette (1024) + LP table (1536) + padding
    // Each LP starts at: 0xb00 + (pageNumber * 0x10000)
    uint32_t offset = 0xb00 + (pageNumber * 0x10000);
    
    printf("loadPage: seek to %u (0x%x)\n", offset, offset);
    
    // Seek to this large page
    klseek(animStream->handle, offset, SEEK_SET);
    
    // Read LP descriptor (6 bytes)
    kread(animStream->handle, &animStream->curLp, sizeof(lp_descriptor_t));
    
    printf("loadPage: baseRecord=%d, nRecords=%d, nBytes=%d\n", 
           animStream->curLp.baseRecord, animStream->curLp.nRecords, animStream->curLp.nBytes);
    
    // Skip the 2-byte padding after descriptor
    klseek(animStream->handle, offset + sizeof(lp_descriptor_t) + sizeof(uint16_t), SEEK_SET);
    
    // Read LP data (record offsets + compressed data)
    int32_t toRead = animStream->curLp.nBytes + (animStream->curLp.nRecords * 2);
    printf("loadPage: toRead=%d\n", toRead);
    if (toRead > 0x10000) toRead = 0x10000;  // Safety limit
    kread(animStream->handle, animStream->pageBuffer, toRead);
    
    animStream->curLpNum = pageNumber;
    printf("loadPage done\n");
    return true;
}

// Render a frame from the currently loaded large page
static void renderFrame(uint16_t frameNumber)
{
    if (!animStream) return;
    
    uint16_t destFrame = frameNumber - animStream->curLp.baseRecord;
    uint16_t offset = 0;
    
    // Sum up offsets to find this frame's data
    for (uint16_t i = 0; i < destFrame; i++) {
        offset += animStream->pageBuffer[i];
    }
    
    uint8_t *ppointer = (uint8_t *)animStream->pageBuffer;
    ppointer += animStream->curLp.nRecords * 2 + offset;
    
    // Handle frame header
    if (ppointer[1]) {
        ppointer += (4 + (((uint16_t *)ppointer)[1] + (((uint16_t *)ppointer)[1] & 1)));
    } else {
        ppointer += 4;
    }
    
    CPlayRunSkipDump(ppointer, animStream->imageBuffer);
}

// Draw a single frame (internal)
static void drawFrame(uint16_t frameNumber)
{
    if (!loadPage(findPage(frameNumber))) return;
    renderFrame(frameNumber);
}

//=============================================================================
// Public API
//=============================================================================

bool AnimStream_Open(const char *filename)
{
    // Close any existing animation
    AnimStream_Close();
    
    // Allocate state structure in PSRAM
    animStream = (anim_stream_t *)psram_malloc(sizeof(anim_stream_t));
    if (!animStream) {
        printf("AnimStream: Failed to allocate state\n");
        return false;
    }
    memset(animStream, 0, sizeof(anim_stream_t));
    animStream->handle = -1;
    animStream->curLpNum = 0xFFFF;
    animStream->currentFrame = -1;
    
    // Allocate buffers in PSRAM
    animStream->pageBuffer = (uint16_t *)psram_malloc(0x10000);  // 64KB for LP
    animStream->imageBuffer = (uint8_t *)psram_malloc(0x10000);  // 64KB for frame
    
    if (!animStream->pageBuffer || !animStream->imageBuffer) {
        printf("AnimStream: Failed to allocate buffers\n");
        AnimStream_Close();
        return false;
    }
    
    // Clear image buffer
    memset(animStream->imageBuffer, 0, 0x10000);
    
    // Open the file
    animStream->handle = TCkopen4load(filename, 0);
    if (animStream->handle < 0) {
        printf("AnimStream: Failed to open %s\n", filename);
        AnimStream_Close();
        return false;
    }
    
    // Read header
    kread(animStream->handle, &animStream->header, sizeof(lpfileheader_t));
    
    // Skip to palette - in original code: buffer += sizeof(lpfileheader) + 128 = 256 bytes from start
    klseek(animStream->handle, 256, SEEK_SET);
    
    // Read palette (1024 bytes = 256 * 4, at offset 256 after header + padding)
    // File format: BGRA (4 bytes per entry)
    uint8_t palTemp[1024];
    kread(animStream->handle, palTemp, 1024);
    
    // Debug: print first few palette entries
    printf("AnimStream: Raw palette[0-3]: %02x %02x %02x %02x\n", 
           palTemp[0], palTemp[1], palTemp[2], palTemp[3]);
    printf("AnimStream: Raw palette[4-7]: %02x %02x %02x %02x\n", 
           palTemp[4], palTemp[5], palTemp[6], palTemp[7]);
    
    // The original animlib.c does:
    // pal[i+2] = file[0] (first byte)
    // pal[i+1] = file[1] (second byte)  
    // pal[i+0] = file[2] (third byte)
    // So it's reversing the order: file is RGB(A), stored as BGR in pal
    for (int i = 0; i < 768; i += 3) {
        int fileIdx = (i / 3) * 4;  // 4 bytes per entry in file
        animStream->palette[i + 2] = palTemp[fileIdx + 0];  // First byte -> position 2
        animStream->palette[i + 1] = palTemp[fileIdx + 1];  // Second byte -> position 1
        animStream->palette[i + 0] = palTemp[fileIdx + 2];  // Third byte -> position 0
        // Skip byte 4 (alpha)
    }
    
    printf("AnimStream: Converted pal[0-2]: %02x %02x %02x\n",
           animStream->palette[0], animStream->palette[1], animStream->palette[2]);
    
    // Seek to LP table (at lpfTableOffset, typically 1280)
    printf("AnimStream: lpfTableOffset=%d, nLps=%d\n", 
           animStream->header.lpfTableOffset, animStream->header.nLps);
    klseek(animStream->handle, animStream->header.lpfTableOffset, SEEK_SET);
    
    // Read LP descriptors
    kread(animStream->handle, animStream->lpArray, sizeof(lp_descriptor_t) * animStream->header.nLps);
    
    // Debug: print first few LP descriptors
    for (int i = 0; i < 3 && i < animStream->header.nLps; i++) {
        printf("  LP[%d]: baseRecord=%d, nRecords=%d, nBytes=%d\n",
               i, animStream->lpArray[i].baseRecord, 
               animStream->lpArray[i].nRecords, animStream->lpArray[i].nBytes);
    }
    
    printf("AnimStream: Opened %s (%u frames, %dx%d)\n", 
           filename, animStream->header.nRecords,
           animStream->header.width, animStream->header.height);
    
    return true;
}

void AnimStream_Close(void)
{
    if (animStream) {
        if (animStream->handle >= 0) {
            kclose(animStream->handle);
        }
        // Note: PSRAM allocations are freed when PSRAM is reset
        // For explicit cleanup, we'd need psram_free()
        animStream = NULL;
    }
}

int AnimStream_NumFrames(void)
{
    if (!animStream) return 0;
    return animStream->header.nRecords;
}

uint8_t *AnimStream_GetPalette(void)
{
    if (!animStream) return NULL;
    return animStream->palette;
}

uint8_t *AnimStream_DrawFrame(int framenumber)
{
    if (!animStream) return NULL;
    
    printf("AnimStream_DrawFrame(%d), cur=%d\n", framenumber, animStream->currentFrame);
    
    // Frames are 0-indexed internally but 1-indexed in the API
    // Actually ANM files use 0-indexed frames
    
    // Draw all frames from current to target (delta compression)
    if (animStream->currentFrame != -1 && animStream->currentFrame <= framenumber) {
        for (int i = animStream->currentFrame; i < framenumber; i++) {
            drawFrame(i);
        }
    } else {
        // Need to start from beginning
        printf("AnimStream: Building from frame 0 to %d\n", framenumber);
        for (int i = 0; i < framenumber; i++) {
            drawFrame(i);
        }
    }
    
    animStream->currentFrame = framenumber;
    printf("AnimStream_DrawFrame done\n");
    return animStream->imageBuffer;
}

int AnimStream_GetWidth(void)
{
    if (!animStream) return 320;
    return animStream->header.width;
}

int AnimStream_GetHeight(void)
{
    if (!animStream) return 200;
    return animStream->header.height;
}
