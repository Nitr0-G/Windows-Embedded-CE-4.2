//
// Copyright (c) Microsoft Corporation.  All rights reserved.
//
//
// This source code is licensed under Microsoft Shared Source License
// Version 1.0 for Windows CE.
// For a copy of the license visit http://go.microsoft.com/fwlink/?LinkId=3223.
//

/**************************************************************************


Module Name:

   global.cpp

Abstract:

   Code used by the entire GDI test suite

***************************************************************************/

//#define VISUAL_CHECK

#include "global.h"

BOOL g_fPrinterDC = FALSE;
int g_iPrinterCntr = 0;

// see global.h to adjust size
int     myStockObj[numStockObj] = {
    NULL_BRUSH,
    WHITE_BRUSH,
    LTGRAY_BRUSH,
    GRAY_BRUSH,
    DKGRAY_BRUSH,
    BLACK_BRUSH,
    SYSTEM_FONT,
    DEFAULT_PALETTE,
};

COLORREF PegPal[4] = {
    RGB(0x0, 0x0, 0x0),
    RGB(0x55, 0x55, 0x55),
    RGB(0xaa, 0xaa, 0xaa),
    RGB(0xff, 0xff, 0xff),
};

NameValPair rgBitmapTypes[numBitMapTypes] = {
    {compatibleBitmap, TEXT("CreateCompatibleBitmap")},
    {lb, TEXT("LoadBitmap")},
    {DibRGB, TEXT("CreateDIBSection RGB")},
    {DibPal, TEXT("CreateDIBSection Pal")}
};

/***********************************************************************************
***
***   UNDER_CE Work Arounds
***
************************************************************************************/

//**********************************************************************************
// UNDER_CE Pending GdiSetBatchLimit
void
pegGdiSetBatchLimit(int i)
{
#ifndef UNDER_CE
    GdiSetBatchLimit(i);
#endif // UNDER_CE
}

//**********************************************************************************
// UNDER_CE Pending GdiSetBatchLimit
void
pegGdiFlush(void)
{
#ifndef UNDER_CE
    GdiFlush();
#endif // UNDER_CE
}

/***********************************************************************************
***
***   myGetSystemMetrics
***
************************************************************************************/

//***********************************************************************************
int
myGetSystemMetrics(int flag)
{

    extern DWORD screenSize;    // decl in specdc.cpp
    int     result = 0;

    switch (flag)
    {
        case SM_CXSCREEN:
            result = LOWORD(screenSize);
            break;
        case SM_CYSCREEN:
            result = HIWORD(screenSize);
            break;
    }
    if (!result)
        result = GetSystemMetrics(flag);
    return result;
}

inline int myGetBitDepth()
{
    return gBpp;
}

/***********************************************************************************
***
***   GetLastError
***
************************************************************************************/

//**********************************************************************************
BOOL myGetLastError(DWORD expected)
{
    DWORD   result;

    result = GetLastError();
    if (errorFlag == EErrorCheck && result != expected)
    {
        info(FAIL, TEXT("GetLastError returned:0x%x[%d] expected:0x%x[%d]"), result, result, expected, expected);
        return TRUE;
    }
    return FALSE;
}

//**********************************************************************************
void
mySetLastError(void)
{
    SetLastError(NADA);
}

//**********************************************************************************
TDC myCreateCompatibleDC(TDC oHdc)
{

    mySetLastError();
    TDC     hdc = CreateCompatibleDC(oHdc);

    myGetLastError(NADA);

    pegGdiSetBatchLimit(1);
    if (!hdc)
        info(FAIL, TEXT("CreateCompatibleDC Failed"));

    if (gSanityChecks)
        info(DETAIL, TEXT("CreateCompatibleDC"));

    return hdc;
}

//**********************************************************************************
void
myDeleteDC(TDC hdc)
{

    mySetLastError();
    BOOL    result = DeleteDC(hdc);

    myGetLastError(NADA);

    if (!result)
        info(FAIL, TEXT("DeleteDC Failed"));

    if (gSanityChecks)
        info(DETAIL, TEXT("DeleteDC"));
}

/***********************************************************************************
***
***   Checks pixel for in bounds and whiteness
***
************************************************************************************/

//**********************************************************************************
BOOL isWhitePixel(TDC hdc, DWORD color, int x, int y, int pass)
{
    int     nBits = myGetBitDepth();

    if (((x >= zx || y >= zy || x < 0 || y < 0)) && color == CLR_INVALID)
        return pass;

    // otherwise check against white

    // Sh3: SED1354: 3-3-2 mode: WHITE = 0xC0E0E0
    if (nBits == 8 && (color == 0xC0E0E0 || color == 0xFFFFFF))
        return TRUE;

    // CEPC: 16 bits: 5-6-5 mode: white = 0xF8FCF8
    // CEPC: 16 bits: 5-5-5 mode: white = 0xF8F8F8
    if (nBits == 16 && (color == 0xF8FCF8 || color == 0xF8F8F8 || color == 0xFFFFFF))
        return TRUE;

    // for 32bpp, ignore the alpha.
    if (nBits == 32)
        color = (color & 0xFFFFFF);

    return color == 0xFFFFFF;
}

/***********************************************************************************
***
***   Verification Support
***
************************************************************************************/

//***********************************************************************************
void
setRGBQUAD(RGBQUAD * rgbq, int r, int g, int b)
{
    rgbq->rgbBlue = (BYTE)b;
    rgbq->rgbGreen = (BYTE)g;
    rgbq->rgbRed = (BYTE)r;
    rgbq->rgbReserved = 0;
}

//**********************************************************************************
BOOL
CheckScreenHalves(TDC hdc)
{
    BOOL    bRval = TRUE;
    int     bpp = myGetBitDepth(),
            plus,
            step = (bpp / 8);
    DWORD   maxErrors = 20,
            dwMask = (32 == bpp) ? 0x00FFFFFF : (0xFFFFFFFF >> (32 - bpp)); // 32 bits --> ignore alpha :
                                                                            // BLUE  = 0x000000FF
                                                                            // Green = 0x0000FF00
                                                                            // RED   = 0x00FF0000

    BYTE   *lpBits = NULL,
           *pBitsExpected,
           maskbpp[4][8] ={ { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 }, // 1bpp
                            { 0xC0, 0x30, 0x0C, 0x03, 0x00, 0x00, 0x00, 0x00 }, // 2bpp
                            { 0xF0, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // 4bpp
                            { 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } }; // 8bpp
    TDC     myHdc;
    TBITMAP hBmp,
            stockBmp;
    DIBSECTION ds;
    DWORD   fLExpected,
            fRExpected,
            tempDWORD_l = 0,
            tempDWORD_r = 0,
            dwError;

    if (g_fPrinterDC)
    {
        info (ECHO, TEXT("CheckScreenHalves skipped running on printer DC"));
        return TRUE;
    }
    
    info(DETAIL, TEXT("CheckScreenHalves: bpp=%d"), bpp);
    if (bpp < 8)
        step = 1;

    if (NULL == (myHdc = CreateCompatibleDC(hdc)))
    {
        dwError = GetLastError();
        if (dwError == ERROR_NOT_ENOUGH_MEMORY || dwError == ERROR_OUTOFMEMORY)
            info(DETAIL, TEXT("CreateCompatibleDC(hdc) fail: Out of memory:  bpp=%d err=%ld"), bpp, dwError);
        else
        {
            info(FAIL, TEXT("CreateCompatibleDC(hdc) fail: bpp=%d err=%ld"), bpp, dwError);
            bRval = FALSE;
        }
        return bRval;
    }

    

    if (NULL == (hBmp = myCreateDIBSection(hdc, (VOID **) & lpBits, bpp, zx, -(zy+1), bpp <= 8  ? DIB_PAL_COLORS : DIB_RGB_COLORS)))
    {
        dwError = GetLastError();
        if (dwError == ERROR_NOT_ENOUGH_MEMORY || dwError == ERROR_OUTOFMEMORY)
            info(DETAIL, TEXT("myCreateDIBSection() fail: Out of memory: bpp=%d err=%ld"), bpp, dwError);
        else
        {
            info(FAIL, TEXT("myCreateDIBSection() fail: bpp=%d err=%ld"), bpp, dwError);
            bRval = FALSE;
        }
        DeleteDC(myHdc);
        return bRval;
    }

    // copy the bits
    stockBmp = (TBITMAP) SelectObject(myHdc, hBmp);
    GetObject(hBmp, sizeof (DIBSECTION), &ds);
    if (ds.dsBm.bmHeight < 0)
    {
       info(FAIL, TEXT("CheckScreenHalves - GetObject returned bmp with negative height"));
       bRval = FALSE;
    }

    BitBlt(myHdc, 0, 0, zx, zy, hdc, 0, 0, SRCCOPY);

    // if we're 8, 16, 24, or 32 bits per pixel, then there is one or more bytes per pixel
    if (bpp > 8)
    {
        for (int y = 0; y < zy && maxErrors > 0; y++)
        {
            // point the expected and actual pixel pointers to the beginning of the scanline
            pBitsExpected = lpBits + y * ds.dsBm.bmWidthBytes;
            for (int x = 0; x < zx / 2 && maxErrors > 0; x++, pBitsExpected += step)
            {
                tempDWORD_l = *(DWORD UNALIGNED *) pBitsExpected;
                fLExpected = tempDWORD_l & dwMask;
                tempDWORD_r = *((DWORD UNALIGNED *) (pBitsExpected + (step * zx / 2)));
                fRExpected = tempDWORD_r & dwMask;

                //increment error count if pixel is different and log error
                if (fLExpected != fRExpected)
                {
                    --maxErrors;
                    bRval = FALSE;
                }
                
                if (fLExpected != fRExpected && maxErrors > 0)
                    info(FAIL, TEXT("L != R, L:(%d, %d)=(0x%08X) R:(%d, %d)=(0x%08X)"), x, y, fLExpected, x + zx / 2, y, fRExpected);
                else if (maxErrors <= 0)
                    info(FAIL, TEXT("Exiting due to large number of errors"));
            }
        }
    }
    // 1, 2, or 4 bits per pixel, more than 1 pixel per byte
    else
    {
        int ppbyte = 8 / bpp;
        // index into the mask array, index 0 for 1bpp, index 1 for 2bpp, index 2 for 4bpp
        int bppindex = 0;
        switch(bpp)
        {
            case 1:
                bppindex = 0;
                break;
            case 2:
                bppindex = 1;
                break;
            case 4:
                bppindex = 2;
                break;
            case 8:
                bppindex = 3;
                break;
        }

        plus = ds.dsBm.bmWidthBytes / 2;

        for (int y = 0; y < zy && maxErrors > 0; y++)
        {
            pBitsExpected = lpBits + y * ds.dsBm.bmWidthBytes;
            for (int x = 0; x < zx / 2 && maxErrors > 0; x++)
            {
                // x/ppbyte is the byte that the pixel x is in, for example, if x=0-7 and  ppbyte = 8 (1bpp), x/ppbyte is 0
                // x%ppbyte is the pixel position within the byte.
                int pBitsIndex = x / ppbyte;
                int maskIndex = x % ppbyte;
                fLExpected = pBitsExpected[pBitsIndex] & maskbpp[bppindex][maskIndex];
                fRExpected = pBitsExpected[pBitsIndex + plus] & maskbpp[bppindex][maskIndex];

                if((int) fLExpected != (int) fRExpected)
                {
                    maxErrors--;
                    bRval = FALSE;
                }
                
                if ((int) fLExpected != (int) fRExpected && maxErrors > 0)
                    info(FAIL, TEXT("L != R, L:(%d, %d)=(0x%06X) R:(%d, %d)=(0x%06X)"), x, y, fLExpected, x + zx/2, y, fRExpected);
                else if (maxErrors <= 0)
                    info(FAIL, TEXT("Exiting due to large number of errors"));
            }
        }
    }

    // print out a bitmap if we failed.
    if(!bRval)
        OutputBitmap(myHdc);
    
    SelectObject(myHdc, stockBmp);
    DeleteObject(hBmp);
    DeleteDC(myHdc);
    return bRval;
}

/***********************************************************************************
***
***   Make sure entire screen is white
***
************************************************************************************/

//**********************************************************************************
int
CheckAllWhite(TDC hdc, BOOL alwaysPass, BOOL complete, BOOL quiet, int expected)
{
    int     bpp = myGetBitDepth(),
            step;
    DWORD   maxErrors = 20,
            dwErrors = 0,
            dwMask = 0,
            dwWhite = 0,
            dwTemp,
            dwError;
    BOOL    bMatch = FALSE;
    BYTE   *lpBits = NULL,
           *pBitsExpected;
    TDC     myHdc;
    TBITMAP hBmp,
            stockBmp;
    DIBSECTION ds;

    //info(DETAIL, TEXT("CheckAllWhite: bpp=%d:  dwMask=0x%08lX: \r\n"), bpp, dwMask);

    if (g_fPrinterDC)
    {
        info (ECHO, TEXT("CheckAllWhite skipped using printer DC"));
        return expected;
    }

    // compensate for color conversions and bit formats, since we always check in 16bpp+
    
    // paletted, to RGB 565 is 0xFFFF
    // RGB 565 to RGB 565 results in 0xFFFF
    if(bpp <= 8 || (16 ==bpp && 0x00F8FCF8 == gcrWhite))
        dwWhite = 0xFFFF;
    // RGB 555 to RGB 565 results in 0xFFDF
    if(16 == bpp && 0x00F8F8F8 == gcrWhite)
        dwWhite = 0xFFDF;
    // RGB 888 to RGB 888 results in 0x00FFFFFF, ignore the alpha
    else if(24 == bpp || 32 == bpp)
        dwWhite = 0x00FFFFFF;
    
    // always do the Checkallwhite with RGB values, comparing palette entry numbers doesn't make sense.
    bpp = max(bpp, 16);
    step = (bpp / 8);
    
    if(16 == bpp)
        dwMask = 0xFFFF;
    else dwMask = 0x00FFFFFF;

    if (NULL == (myHdc = CreateCompatibleDC(hdc)))
    {
        dwError = GetLastError();
        if (dwError == ERROR_NOT_ENOUGH_MEMORY || dwError == ERROR_OUTOFMEMORY)
            info(DETAIL, TEXT("CreateCompatibleDC(hdc) fail: Out of memory: err=%ld"), dwError);
        else
            info(FAIL, TEXT("CreateCompatibleDC(hdc) fail: err=%ld"), dwError);
        return 0;
    }

    if (NULL == (hBmp = myCreateDIBSection(hdc, (VOID **) & lpBits, bpp, zx, -zy, DIB_RGB_COLORS)))
    {
        dwError = GetLastError();
        if (dwError == ERROR_NOT_ENOUGH_MEMORY || dwError == ERROR_OUTOFMEMORY)
            info(DETAIL, TEXT("myCreateDIBSection() fail: Out of memory: err=%ld"), dwError);
        else
            info(FAIL, TEXT("myCreateDIBSection() fail: err=%ld"), dwError);
        DeleteDC(myHdc);
        return 0;
    }

    // copy the bits
    stockBmp = (TBITMAP) SelectObject(myHdc, hBmp);
    GetObject(hBmp, sizeof (DIBSECTION), &ds);
    if (ds.dsBm.bmHeight < 0)
        info(FAIL, TEXT("CheckScreenHalves - GetObject returned bmp with negative height"));

    BitBlt(myHdc, 0, 0, zx, zy, hdc, 0, 0, SRCCOPY);

    for (int y = 0; y < zy - 1 && (dwErrors <= maxErrors || complete); y++)
    {
        // point the expected and actual pixel pointers to the beginning of the scanline
        pBitsExpected = lpBits + y * ds.dsBm.bmWidthBytes;

        for (int x = 0; x < zx && (dwErrors <= maxErrors || complete); x++, pBitsExpected += step)
        {
            dwTemp = *(DWORD UNALIGNED *) pBitsExpected;
            bMatch = ((dwTemp & dwMask) == dwWhite);
            if (!bMatch)
            {
                dwErrors++;
                // increment error count if pixel is different and log error
                if (dwErrors < maxErrors)
                {
                     if (!quiet)
                        info((alwaysPass) ? ECHO : FAIL, TEXT("(%d, %d) Found (0x%06X)"), x, y, dwTemp & dwMask);
                }
                else if (!quiet && !complete && dwErrors == maxErrors)
                    info((alwaysPass) ? ECHO : FAIL, TEXT("Exiting due to large number of errors"));
            }
        }
    }

    SelectObject(myHdc, stockBmp);
    DeleteObject(hBmp);
    DeleteDC(myHdc);

    return (int) dwErrors;
}

/***********************************************************************************
***
***   Looks for black on inside of rect and white around outside
***
************************************************************************************/

//**********************************************************************************
void
goodBlackRect(TDC hdc, RECT * r)
{

    int     midX = ((*r).right + (*r).left) / 2,    // midpoints on rect

            midY = ((*r).bottom + (*r).top) / 2;
    POINT   white[4],
            black[4];
    DWORD   result;

    if (g_fPrinterDC)
    {
        info(ECHO, TEXT("goodBlackRect skipped running on printer DC"));
        return;
    }

    // pixels that are expected white
    white[0].x = (*r).left - 1;
    white[0].y = midY;
    white[1].x = midX;
    white[1].y = (*r).top - 1;
    white[2].x = (*r).right + 1;
    white[2].y = midY;
    white[3].x = midX;
    white[3].y = (*r).bottom + 1;

    // pixels that are expected black
    black[0].x = (*r).left + 1;
    black[0].y = midY;
    black[1].x = midX;
    black[1].y = (*r).top + 1;
    black[2].x = (*r).right - 2;
    black[2].y = midY;
    black[3].x = midX;
    black[3].y = (*r).bottom - 2;

    for (int i = 0; i < 4; i++)
    {
        // looks on outside edges for white
        result = GetPixel(hdc, white[i].x, white[i].y);

        if (!isWhitePixel(hdc, result, white[i].x, white[i].y, 1))
        {
            info(FAIL, TEXT("On RECT (%d %d %d %d) found non-white Pixel:0x%x at %d %d"), (*r).left, (*r).top, (*r).right,
                 (*r).bottom, result, white[i].x, white[i].y);
        }

        // looks on inside for black
        result = GetPixel(hdc, black[i].x, black[i].y);

        if (isWhitePixel(hdc, result, black[i].x, black[i].y, 0))
            info(FAIL, TEXT("On RECT (%d %d %d %d) found white Pixel:0x%x at %d %d"), (*r).left, (*r).top, (*r).right,
                 (*r).bottom, result, black[i].x, black[i].y);
    }
}

/***********************************************************************************
***
***   Random number generators
***
************************************************************************************/
static int initialPassFlag = 0;

//**********************************************************************************
int
goodRandomNum(int range)
{                               // return 0 to (num-1)
    return abs(rand()) % range;
}

//**********************************************************************************
int
badRandomNum(void)
{
    return rand() * ((rand() % 2) ? -1 : 1);
}

//**********************************************************************************
DWORD randColorRef(void)
{
    return RGB(rand() % 255, rand() % 255, rand() % 255);
}

/***********************************************************************************
***
***   RECT functions
***
************************************************************************************/
void
setRect(RECT * aRect, int l, int t, int r, int b)
{
    (*aRect).left = l;
    (*aRect).top = t;
    (*aRect).right = r;
    (*aRect).bottom = b;
}

//***********************************************************************************
BOOL isEqualRect(RECT * a, RECT * b)
{
    return (*a).top == (*b).top && (*a).left == (*b).left && (*a).right == (*b).right && (*a).bottom == (*b).bottom;
}

void
fixNum(long *n)
{
#ifdef UNDER_CE
    if (abs(*n) > 0xFFF)
    {                           // only can hold 6bit values
        *n = *n % 0xFFF;
        //info(ECHO,TEXT("pre:%X post:%X post:%d"),x, *n, *n);
    }
#endif // UNDER_CE
}

//***********************************************************************************
void
randomRect(RECT * temp)
{
    (*temp).left = rand() * ((rand() % 2) ? -1 : 1);
    (*temp).top = rand() * ((rand() % 2) ? -1 : 1);
    (*temp).right = rand() * ((rand() % 2) ? -1 : 1);
    (*temp).bottom = rand() * ((rand() % 2) ? -1 : 1);

    fixNum(&((*temp).left));
    fixNum(&((*temp).top));
    fixNum(&((*temp).right));
    fixNum(&((*temp).bottom));
}

/***********************************************************************************
***
***   setTestRectRgn
***
************************************************************************************/
RECT    rTests[NUMREGIONTESTS];
RECT    center;

//***********************************************************************************
void
setTestRectRgn(void)
{

    int     sx = 50,
            sy = 50,
            gx = 100 + sx,
            gy = 100 + sy,
            ox = (gx - sx) / 4,
            oy = (gy - sy) / 4,
            x1 = ox,
            y1 = oy,
            x2 = x1 + ox * 2,
            y2 = y1 + oy * 2,
            x3 = x2 + ox * 2,
            y3 = y2 + oy * 2,
            x4 = x3 + ox * 2,
            y4 = y3 + oy * 2;

    setRect(&center, sx, sy, gx, gy);

    // interesting rect positions
    setRect(&rTests[0], x1, y1, x2, y2);
    setRect(&rTests[1], x2, y1, x3, y2);
    setRect(&rTests[2], x3, y1, x4, y2);
    setRect(&rTests[3], x1, y2, x2, y3);
    setRect(&rTests[4], x2, y2, x3, y3);
    setRect(&rTests[5], x3, y2, x4, y3);
    setRect(&rTests[6], x1, y3, x2, y4);
    setRect(&rTests[7], x2, y3, x3, y4);
    setRect(&rTests[8], x3, y3, x4, y4);
    setRect(&rTests[9], gx + ox, sy, gx * 2 - sx + ox, gy);
    setRect(&rTests[10], x1, y1, sx + 1, sy + 1);
    setRect(&rTests[11], gx - 1, y1, x4, sy + 1);
    setRect(&rTests[12], x1, gy - 1, sx + 1, y4);
    setRect(&rTests[13], gx - 1, gy - 1, x4, y4);
    setRect(&rTests[14], sx, sy, gx, gy);
}

/***********************************************************************************
***
***   Palette Functions
***
************************************************************************************/

//**********************************************************************************
HPALETTE myCreatePal(TDC hdc, int code)
{
    HPALETTE hPal,
            stockPal;
    WORD    nEntries;
    int     i;

    struct
    {
        WORD    Version;
        WORD    NumberOfEntries;
        PALETTEENTRY aEntries[256];
    }
    Palette =
    {
        0x300,                  // version number
        256                     // number of colors
    };

    // set up palette
    hPal = (HPALETTE) GetCurrentObject(hdc, OBJ_PAL);
    GetObject(hPal, sizeof (nEntries), &nEntries);
    GetPaletteEntries(hPal, 0, nEntries, (LPPALETTEENTRY) & (Palette.aEntries));

    // copy bottom half of def sys-pal to bottom of new pal
    for (i = 246; i < 256; i++)
        Palette.aEntries[i] = Palette.aEntries[i - 236];

    for (i = 10; i < 246; i++)
        Palette.aEntries[i].peRed = Palette.aEntries[i].peGreen = Palette.aEntries[i].peBlue = (BYTE)(i + code);

    hPal = CreatePalette((LPLOGPALETTE) & Palette);
    if (!hPal)
        info(FAIL, TEXT("CreatePalette returned 0"));

    stockPal = SelectPalette(hdc, hPal, 0);
    if (!stockPal)
        info(FAIL, TEXT("SelectPalette returned NULL hPal"));

    if (!hPal || !stockPal)
        info(FAIL, TEXT("myCreatePal: [%s%s] Invalid"), (!hPal) ? L"hPal" : L"", (!stockPal) ? L" stockPal" : L"");
    RealizePalette(hdc);

    return hPal;
}

//**********************************************************************************
HPALETTE myCreateNaturalPal(TDC hdc)
{
    //REVIEW copy in Natural Pal
    HPALETTE hPal;

    struct
    {
        WORD    Version;
        WORD    NumberOfEntries;
        PALETTEENTRY aEntries[236];
    }
    Palette = { 0x300, 256, { 0 } };

    Palette;

    // set up palette
    hPal = (HPALETTE) GetStockObject(DEFAULT_PALETTE);

    if (!hPal)
        info(FAIL, TEXT("GetStockObject returned 0"));

    return hPal;
}

  //**********************************************************************************
void
myDeletePal(TDC hdc, HPALETTE hPal)
{

    HPALETTE hPalRet = SelectPalette(hdc, (HPALETTE) GetStockObject(DEFAULT_PALETTE), 0 /* CE ignore this */ );

    if (!hPalRet)
        info(FAIL, TEXT("myDeletePal: SelectPalette(hPal) failed: err=%ld"), GetLastError());

    if (!DeleteObject(hPal))
        info(FAIL, TEXT("myDeletePal: DeleteObject(hPal) failed: err=%ld"), GetLastError());

    RealizePalette(hdc);
}

/***********************************************************************************
***
***   Font Support Functions
***
************************************************************************************/
int     tahomaFont;
int     courierFont;
int     symbolFont;
int     timesFont;
int     wingdingFont;
int     verdanaFont;
int     aFont;
int     MSLOGO;
int     numFonts;
int     arialRasterFont;

fontInfo fontArray[MAX_FONT_ARRAY];

// since aFont is set to the first valid font, we want all of the non-symbolic (tahoma, etc) truetype fonts
// ahead of the symbolic ones (wingdings, etc).
static struct fontInfo privateFontArray[] = {
    TEXT("tahoma.ttf"),     33956,  TEXT("Tahoma"),             ETrueType,  1,
    TEXT("cour.ttf"),        3400,  TEXT("Courier New"),        ETrueType,  1,
    TEXT("times.ttf"),      49288,  TEXT("Times New Roman"),    ETrueType,  1,
    TEXT("verdana.ttf"),    53268,  TEXT("Verdana"),            ETrueType,  1,
    TEXT("nina.ttf"),               0,  TEXT("Nina"),            ETrueType,  0,
    TEXT("symbol.ttf"),     59776,  TEXT("Symbol"),             ETrueType,  1,
    TEXT("wingding.ttf"),   54468,  TEXT("Wingdings"),          ETrueType,  1,
    TEXT("mslogo.ttf"),     53268,  TEXT("Microsoft Logo"),     ETrueType,  0,
    TEXT("arial.fnt"),       6024,  TEXT("Arial"),              ERaster,    1,
    TEXT("arial.fon"),      53504,  TEXT("Arial"),              ERaster,    0,
    TEXT("times.fnt"),      13617,  TEXT("Times New Roman"),    ERaster,    0,
    TEXT("times.fon"),      59792,  TEXT("Times New Roman"),    ERaster,    0,
};
static const int privateNumFonts = sizeof(privateFontArray)/sizeof(struct fontInfo);

#ifdef UNDER_CE
   static const TCHAR *fontPath  = TEXT("\\Windows\\");
#else
   static const TCHAR *fontPath  = TEXT("C:\\windows\\fonts\\");
//   #error You must provide a real path to where ever your fonts are
#endif


//***********************************************************************************
int     WINAPI
myAddFontResource(LPTSTR szFontName)
{
    DWORD   dw;
    int     n;

    SetLastError(0);
    if ((n = AddFontResource(szFontName)) == 0)
    {
        dw = GetLastError();
        // invalid data is returned if a ttf or raster font is available, but the image doesn't support
        // the that font data. (i.e. tahoma.ttf is in a raster font image because the sysgen is set)
        if (dw == ERROR_FILE_NOT_FOUND || dw == ERROR_INVALID_DATA)
        {
            // fake.usa, fale.ttf: expect failed
            if ((_tcsicmp(szFontName, TEXT("\\Windows\\fake.usa")) == 0) ||
                (_tcsicmp(szFontName, TEXT("\\Windows\\fake.ttf")) == 0))
                info(DETAIL, TEXT("AddFontResource: %s: expect failure"), szFontName);
            else
                info(DETAIL, TEXT("AddFontResource failed on %s: font is missing in ROM"), szFontName);
        }
        else
            info(FAIL, TEXT("AddFontResource failed on %s: err=%d"), szFontName, dw);
    }
    return n;
}

//***********************************************************************************
HFONT setupFont(BOOL AddFont, int i, TDC hdc, long Hei, long Wid, long Esc, long Ori, long Wei, BYTE Ita, BYTE Und, BYTE Str)
{

    LOGFONT lFont;
    HFONT   hFont;

    Hei = RESIZE(Hei);
    Wid = RESIZE(Wid);

    if (AddFont && (myAddFontResource(fontArray[i].fileName) == 0))
        return NULL;

    // set up log info
    lFont.lfHeight = Hei;
    lFont.lfWidth = Wid;
    lFont.lfEscapement = Esc;
    lFont.lfOrientation = Ori;
    lFont.lfWeight = Wei;
    lFont.lfItalic = Ita;
    lFont.lfUnderline = Und;
    lFont.lfStrikeOut = Str;

    lFont.lfCharSet = DEFAULT_CHARSET;

    lFont.lfOutPrecision = OUT_DEFAULT_PRECIS;
    lFont.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lFont.lfQuality = DEFAULT_QUALITY;
    lFont.lfPitchAndFamily = DEFAULT_PITCH;

    _tcscpy(lFont.lfFaceName, fontArray[i].faceName);
    // create and select in font
    hFont = CreateFontIndirect(&lFont);
    SelectObject(hdc, hFont);
    if (!hFont)
        info(FAIL, TEXT("CreateFontIndirect failed on %s #:%d"), fontArray[i].faceName, i);

    return hFont;
}

//***********************************************************************************
HFONT setupFontMetrics(BOOL AddFont, int i, TDC hdc, long Hei, long Wid, long Esc, long Ori, long Wei, BYTE Ita, BYTE Und,
                       BYTE Str)
{

    int     aResult;
    LOGFONT lFont;
    HFONT   hFont;

    if (AddFont)
    {
        // add font
        aResult = myAddFontResource(fontArray[i].fileName);
        if (fontArray[i].type != EBad && !aResult)
            return NULL;
    }

    // set up log info
    lFont.lfHeight = Hei;
    lFont.lfWidth = Wid;
    lFont.lfEscapement = Esc;
    lFont.lfOrientation = Ori;
    lFont.lfWeight = Wei;
    lFont.lfItalic = Ita;
    lFont.lfUnderline = Und;
    lFont.lfStrikeOut = Str;

    if (_tcscmp(fontArray[i].faceName, TEXT("MS PGothic")) == 0)    // Japanese Font
        lFont.lfCharSet = SHIFTJIS_CHARSET;
    if(_tcscmp(fontArray[i].faceName, TEXT("Symbol")) == 0 || _tcscmp(fontArray[i].faceName, TEXT("Wingdings")) == 0)
        lFont.lfCharSet = SYMBOL_CHARSET;    // these are members of the symbol character set, not the default character set
   else
        lFont.lfCharSet = DEFAULT_CHARSET;
    lFont.lfOutPrecision = OUT_DEFAULT_PRECIS;
    lFont.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lFont.lfQuality = DEFAULT_QUALITY;
    lFont.lfPitchAndFamily = DEFAULT_PITCH;

    _tcscpy(lFont.lfFaceName, fontArray[i].faceName);
    // create and select in font
    hFont = CreateFontIndirect(&lFont);
    SelectObject(hdc, hFont);
    if (!hFont)
        info(FAIL, TEXT("CreateFontIndirect failed on %s index=%d"), fontArray[i].faceName, i);

    return hFont;
}

//***********************************************************************************
void
cleanupFont(BOOL AddFont, int i, TDC hdc, HFONT hFont)
{

    int     rResult;

    if (AddFont)
    {
        rResult = RemoveFontResource(fontArray[i].fileName);
        if (!rResult)
            info(FAIL, TEXT("RemoveFontResource failed on %s #:%d GLE():%d"), fontArray[i].fileName, i, GetLastError());
    }
    SelectObject(hdc, GetStockObject(SYSTEM_FONT));
    DeleteObject(hFont);
}

//***********************************************************************************
static void
makeFileName(TCHAR * dest, const TCHAR * src0, const TCHAR * src1)
{
    dest[0] = 0;
    dest = wcscpy(dest, src0);
    dest = wcscat(dest, src1);
}

//***********************************************************************************
static int
findIndex(TCHAR * src)
{

    TCHAR   temp[MAX_PATH];

    for (int i = 0; i < numFonts; i++)
    {
        makeFileName(temp, fontPath, src);
        if (wcscmp(fontArray[i].fileName, temp) == 0 && fontArray[i].type != EBad)
            return i;
    }
    return -1;
}

//***********************************************************************************
void
initFonts(void)
{
    memset(&fontArray, 0, sizeof (struct fontInfo) * MAX_FONT_ARRAY);

    numFonts = 0;

    for (int i = 0; i < privateNumFonts && i < MAX_FONT_ARRAY; i++)
    {
            fontArray[numFonts].fileName = (TCHAR *) malloc(MAX_PATH * sizeof (TCHAR));
            makeFileName(fontArray[numFonts].fileName, fontPath, privateFontArray[i].fileName);
            fontArray[numFonts].faceName = privateFontArray[i].faceName;
            fontArray[numFonts].fileSize = privateFontArray[i].fileSize;
            fontArray[numFonts].type = privateFontArray[i].type;
            fontArray[numFonts].hardcoded = privateFontArray[i].hardcoded;
            if (0 != AddFontResource(fontArray[numFonts].fileName))
            {
                RemoveFontResource(fontArray[numFonts].fileName);
                info(ECHO, TEXT("Using Good Font: <%s>"), fontArray[numFonts].fileName);
            }
            else
            {
                fontArray[numFonts].type = EBad;
                info(ECHO, TEXT("Using Bad Font: <%s>"), fontArray[numFonts].fileName);
            }
            numFonts++;
    }

    // Make sure that aFont is a valid font
    for (i = 0; i < numFonts; i++)
        if (fontArray[i].type != EBad)
        {
            aFont = i;
            break;
        }
    tahomaFont = findIndex(TEXT("tahoma.ttf"));
    courierFont = findIndex(TEXT("cour.ttf"));
    symbolFont = findIndex(TEXT("symbol.ttf"));
    timesFont = findIndex(TEXT("times.ttf"));
    wingdingFont = findIndex(TEXT("wingding.ttf"));
    verdanaFont = findIndex(TEXT("verdana.ttf"));
    MSLOGO = findIndex(TEXT("mslogo.ttf"));
    arialRasterFont = findIndex(TEXT("arial.fnt"));
    
    // if the logo isn't available, any other font will do
    if(MSLOGO == -1)
        MSLOGO = aFont;
    
    info(ECHO, TEXT("Default Font: <%s>"), fontArray[aFont].fileName);
    info(ECHO, TEXT("fontArray[] contains %d Fonts"), numFonts);
}

//***********************************************************************************
void
freeFonts(void)
{

    for (int i = 0; i < numFonts; i++)
        free(fontArray[i].fileName);
}

/***********************************************************************************
***
***   Simple Drawing Functions
***
************************************************************************************/

//***********************************************************************************
BOOL myPatBlt(TDC hdc, int nXLeft, int nYLeft, int nWidth, int nHeight, DWORD dwRop)
{

    mySetLastError();
    BOOL    result = PatBlt(hdc, nXLeft, nYLeft, nWidth, nHeight, dwRop);

    myGetLastError(NADA);

    if (!result)
        info(FAIL, TEXT("PatBlt returned:%d  0%x0 %d %d %d %d %d"), result, hdc, nXLeft, nYLeft, nWidth, nHeight, dwRop);

    return result;
}

//***********************************************************************************
BOOL myBitBlt(TDC dDC, int dx, int dy, int dwx, int dwy, TDC sDC, int sx, int sy, DWORD op)
{

    if (gSanityChecks)
    {
        info(ECHO, TEXT("Inside myBitBlt"));
        info(DETAIL, TEXT("{%3d,%3d,%3d,%3d,%3d,%3d},"), dx, dy, dwx, dwy, sx, sy);
    }

    if (!dDC || !sDC)
        info(FAIL, TEXT("myBitBlt:one or both DC NULL"));

    mySetLastError();
    BOOL    result = BitBlt(dDC, dx, dy, dwx, dwy, sDC, sx, sy, op);

    myGetLastError(NADA);

    if (!result)
        info(FAIL, TEXT("BitBlt returned %d on(0x%x %d %d %d %d 0x%x %d %d %d) GLE(%d)"), result, dDC, dx, dy, dwx, dwy, sDC,
             sx, sy, op, GetLastError());

    return result;
}

//***********************************************************************************
BOOL    myGradientFill(TDC tdc, PTRIVERTEX pVertex, ULONG dwNumVertex,  PVOID pMesh, ULONG dwNumMesh,   ULONG dwMode)
{

    if (gSanityChecks)
    {
        info(ECHO, TEXT("Inside myGradientFill"));
    }

    mySetLastError();
    BOOL    result = gpfnGradientFill(tdc, pVertex, dwNumVertex, pMesh, dwNumMesh, dwMode);

    myGetLastError(NADA);

    if (!result)
        info(FAIL, TEXT("GradientFill returned %d GLE(%d)"), result, GetLastError());

    return result;

}

//***********************************************************************************
BOOL myMaskBlt(TDC dDC, int dx, int dy, int dwx, int dwy, TDC sDC, int sx, int sy, TBITMAP hBmp, int bwx, int bwy, DWORD op)
{

    if (gSanityChecks)
    {
        info(ECHO, TEXT("Inside myMaskBltExt"));
        info(DETAIL, TEXT("{%3d,%3d,%3d,%3d,%3d,%3d,%X,%d,%d},"), dx, dy, dwx, dwy, sx, sy, hBmp, bwx, bwy);
    }

    if (!dDC || !sDC)
        info(FAIL, TEXT("myMaskBltExt: one or both DC NULL"));

    mySetLastError();
    BOOL    result = MaskBlt(dDC, dx, dy, dwx, dwy, sDC, sx, sy, hBmp, bwx, bwy, op);

    myGetLastError(NADA);

    if (!result)
        info(FAIL, TEXT("MaskBlt returned %d on(0x%x %d %d %d %d 0x%x %d %d %d %X %d %d) GLE(%d)"), result, dDC, dx, dy, dwx,
             dwy, sDC, sx, sy, hBmp, bwx, bwy, op, GetLastError());

    return result;
}

//***********************************************************************************
BOOL myStretchBlt(TDC dDC, int dx, int dy, int dwx, int dwy, TDC sDC, int sx, int sy, int swx, int swy, DWORD op)
{

    if (gSanityChecks)
    {
        info(ECHO, TEXT("Inside myStretchBlt"));
        info(DETAIL, TEXT("{%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d},"), dx, dy, dwx, dwy, sx, sy, swx, swy);
    }

    if (!dDC || !sDC)
        info(FAIL, TEXT("myStretchBlt:one or both DC NULL"));

    mySetLastError();
    BOOL    result = StretchBlt(dDC, dx, dy, dwx, dwy, sDC, sx, sy, swx, swy, op);

    myGetLastError(NADA);

    if (!result)
        info(FAIL, TEXT("StretchBlt returned %d on(0x%x %d %d %d %d 0x%x %d %d %d %d %d) GLE(%d)"),
             result, dDC, dx, dy, dwx, dwy, sDC, sx, sy, swx, swy, op, GetLastError());

    return result;
}

/***********************************************************************************
***
***   passCheck
***
************************************************************************************/

//***********************************************************************************
int
passCheck(int result, int expected, TCHAR * args)
{

    if (result != expected)
    {
        DWORD   dwError = GetLastError();

        if (dwError == ERROR_NOT_ENOUGH_MEMORY || dwError == ERROR_OUTOFMEMORY)
        {
            info(DETAIL, TEXT("returned 0x%lX expected 0x%lX when passed %s:  caused by out of memory: GLE:%d"), result,
                 expected, args, dwError);
            return 1;
        }
        else
        {
            info(FAIL, TEXT("returned 0x%lX expected 0x%lX when passed %s"), result, expected, args);
            return 0;
        }
    }

    return 1;
}

/***********************************************************************************
***
***   pass Empty Rect
***
************************************************************************************/

//***********************************************************************************
void
passEmptyRect(int testFunc, TCHAR * name)
{

    info(ECHO, TEXT("%s: Testing with empty Rect"), name);

    RECT    emptyRect = { 5, 10, 5, 20 };
    HRGN    areaRgn = CreateRectRgn(0, 0, 499, 499);
    TDC     hdc = myGetDC();
    int     result = 1,
            expected = 0;

    PatBlt(hdc, 0, 0, zx, zy, WHITENESS);

    SelectClipRgn(hdc, areaRgn);

    switch (testFunc)
    {
        case EIntersectClipRect:
            result = IntersectClipRect(hdc, emptyRect.left, emptyRect.top, emptyRect.right, emptyRect.bottom);
            expected = NULLREGION;
            break;
        case EExcludeClipRect:
            result = ExcludeClipRect(hdc, emptyRect.left, emptyRect.top, emptyRect.right, emptyRect.bottom);
#ifdef UNDER_CE
            expected = SIMPLEREGION;
#else // UNDER_CE
            expected = COMPLEXREGION;
#endif // UNDER_CE
            break;
    }
    DeleteObject(areaRgn);
    if (result != expected && (testFunc == 1 || testFunc == 0))
        info(FAIL, TEXT("%s returned:%d, expected:%d"), name, result, expected);
    else if (result != expected)
        info(FAIL, TEXT("%s returned:%d, expected:%d"), name, result, expected);
    myReleaseDC(hdc);
}

/***********************************************************************************
***
***   Ask User
***
************************************************************************************/

int     DirtyFlag;

//***********************************************************************************
void
AskMessage(TCHAR * APIname, TCHAR * FuncName, TCHAR * outStr)
{

    int     result = 1;
    TCHAR   questionStr[256];

    DirtyFlag = 0;

    if (verifyFlag == EVerifyAuto || isMemDC())
        return;

    _stprintf(questionStr, TEXT("Did you see %s?"), outStr);

    result = MessageBox(NULL, questionStr, TEXT("Manual Test Verification"), MB_YESNOCANCEL | MB_DEFBUTTON1);

    if (result == IDNO)
        info(FAIL, TEXT("%s in Manual Test<%s> User Replied <NO>:%s"), APIname, FuncName, questionStr);
    else if (result == IDCANCEL)
        verifyFlag = EVerifyAuto;

    DirtyFlag = (verifyFlag == EVerifyAuto) || (result == IDCANCEL);
}

/***********************************************************************************
***
***   Remove All Fonts
***
************************************************************************************/

//***********************************************************************************
void
RemoveAllFonts(void)
{

    for (int i = 0; i < numFonts; i++)
        while (RemoveFontResource(fontArray[i].fileName))
            ;
}

/***********************************************************************************
***
***   Process Create
***
************************************************************************************/

//***********************************************************************************
HANDLE StartProcess(LPCTSTR szCommand)
{
#ifndef UNDER_CE
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    memset(&si, 0, sizeof (si));
    memset(&pi, 0, sizeof (pi));
    si.cb = sizeof (si);

    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWMINNOACTIVE;
    BOOL    fResult = CreateProcess(NULL, (LPTSTR) szCommand, NULL, NULL, FALSE,
                                    CREATE_NEW_CONSOLE | CREATE_DEFAULT_ERROR_MODE | CREATE_NEW_PROCESS_GROUP,
                                    NULL, NULL, &si, &pi);

    return pi.hProcess;
#endif // UNDER_CE
    return NULL;
}

void
MyDelay(int i)
{
#ifdef VISUAL_CHECK
    TCHAR   sz[32];

    wsprintf(sz, TEXT("i=%d"), i);
    MessageBox(NULL, sz, TEXT("Check"), MB_OK);
#endif
}

TCHAR  *
cMapClass::operator[] (int index)
{
    // initialize it to entry 0's string, which is "skip"
    TCHAR  *
        tcTmp = m_funcEntryStruct[0].szName;

    for (int i = 0; NULL != m_funcEntryStruct[i].szName; i++)
        if (m_funcEntryStruct[i].dwVal == (DWORD) index)
            tcTmp = m_funcEntryStruct[i].szName;

    return tcTmp;
}

#define ENTRY(MSG, TestID) {TestID, MSG}

NameValPair funcEntryStruct[] =
{
    // inits
    ENTRY(TEXT("String Not Found"), EInvalidEntry),
    ENTRY(TEXT("Info"), EInfo),
    // verifcation
    ENTRY(TEXT("VerifyManual"), EVerifyManual),
    ENTRY(TEXT("VerifyAuto"), EVerifyAuto),
    // extended verification
    ENTRY(TEXT("DDI_Test.dll verification"), EVerifyDDITEST),
    ENTRY(TEXT("No Verification"), EVerifyNone),
    //extended error checking
    ENTRY(TEXT("ErrorCheck"), EErrorCheck),
    ENTRY(TEXT("ErrorIgnore"), EErrorIgnore),
    // gdi surfaces
    ENTRY(TEXT("GDI_Primary"), EGDI_Primary),
    ENTRY(TEXT("GDI_VidMemory"), EGDI_VidMemory),
    ENTRY(TEXT("GDI_Default"), EGDI_Default),
    ENTRY(TEXT("Win_Primary"), EWin_Primary),
    ENTRY(TEXT("GDI_SysMemory"), EGDI_SysMemory),
    ENTRY(TEXT("GDI_Printer"), EGDI_Printer),
    ENTRY(TEXT("1BPP BITMAP"), EGDI_1bppBMP),
    ENTRY(TEXT("2BPP BITMAP"), EGDI_2bppBMP),
    ENTRY(TEXT("4BPP BITMAP"), EGDI_4bppBMP),
    ENTRY(TEXT("8BPP BITMAP"), EGDI_8bppBMP),
    ENTRY(TEXT("16BPP BITMAP"), EGDI_16bppBMP),
    ENTRY(TEXT("24BPP BITMAP"), EGDI_24bppBMP),
    ENTRY(TEXT("32BPP BITMAP"), EGDI_32bppBMP),
    ENTRY(TEXT("1BPP DIB_Pal"), EGDI_1bppPalDIB),
    ENTRY(TEXT("2BPP DIB_Pal"), EGDI_2bppPalDIB),
    ENTRY(TEXT("4BPP DIB_Pal"), EGDI_4bppPalDIB),
    ENTRY(TEXT("8BPP DIB_Pal"), EGDI_8bppPalDIB),
    ENTRY(TEXT("1BPP DIB_RGB"), EGDI_1bppRGBDIB),
    ENTRY(TEXT("2BPP DIB_RGB"), EGDI_2bppRGBDIB),
    ENTRY(TEXT("4BPP DIB_RGB"), EGDI_4bppRGBDIB),
    ENTRY(TEXT("8BPP DIB_RGB"), EGDI_8bppRGBDIB),
    ENTRY(TEXT("16BPP DIB_RGB"), EGDI_16bppRGBDIB),
    ENTRY(TEXT("24BPP DIB_RGB"), EGDI_24bppRGBDIB),
    ENTRY(TEXT("32BPP DIB_RGB"), EGDI_32bppRGBDIB),
    ENTRY(TEXT("1BPP DIB_Pal BUB"), EGDI_1bppPalDIBBUB),
    ENTRY(TEXT("2BPP DIB_Pal BUB"), EGDI_2bppPalDIBBUB),
    ENTRY(TEXT("4BPP DIB_Pal BUB"), EGDI_4bppPalDIBBUB),
    ENTRY(TEXT("8BPP DIB_Pal BUB"), EGDI_8bppPalDIBBUB),
    ENTRY(TEXT("1BPP DIB_RGB BUB"), EGDI_1bppRGBDIBBUB),
    ENTRY(TEXT("2BPP DIB_RGB BUB"), EGDI_2bppRGBDIBBUB),
    ENTRY(TEXT("4BPP DIB_RGB BUB"), EGDI_4bppRGBDIBBUB),
    ENTRY(TEXT("8BPP DIB_RGB BUB"), EGDI_8bppRGBDIBBUB),
    ENTRY(TEXT("16BPP DIB_RGB BUB"), EGDI_16bppRGBDIBBUB),
    ENTRY(TEXT("24BPP DIB_RGB BUB"), EGDI_24bppRGBDIBBUB),
    ENTRY(TEXT("32BPP DIB_RGB BUB"), EGDI_32bppRGBDIBBUB),
    // ***********************
    // test start
    // clip
    ENTRY(TEXT("ExcludeClipRect"), EExcludeClipRect),
    ENTRY(TEXT("GetClipBox"), EGetClipBox),
    ENTRY(TEXT("GetClipRgn"),EGetClipRgn),
    ENTRY(TEXT("IntersectClipRect"), EIntersectClipRect),
    ENTRY(TEXT("SelectClipRgn"), ESelectClipRgn),
    // draw
    ENTRY(TEXT("BitBlt"), EBitBlt),
    ENTRY(TEXT("ClientToScreen"), EClientToScreen),
    ENTRY(TEXT("CreateBitmap"), ECreateBitmap),
    ENTRY(TEXT("CreateCompatibleBitmap"), ECreateCompatibleBitmap),
    ENTRY(TEXT("CreateDIBSection"), ECreateDIBSection),
    ENTRY(TEXT("Ellipse"), EEllipse),
    ENTRY(TEXT("DrawEdge"), EDrawEdge),
    ENTRY(TEXT("GetPixel"), EGetPixel),
    ENTRY(TEXT("MaskBlt"), EMaskBlt),
    ENTRY(TEXT("PatBlt"), EPatBlt),
    ENTRY(TEXT("Polygon"), EPolygon),
    ENTRY(TEXT("Polyline"), EPolyline),
    ENTRY(TEXT("Rectangle"), ERectangle),
    ENTRY(TEXT("FillRect"), EFillRect),
    ENTRY(TEXT("RectVisible"), ERectVisible),
    ENTRY(TEXT("RoundRect"), ERoundRect),
    ENTRY(TEXT("ScreenToClient"), EScreenToClient),
    ENTRY(TEXT("SetPixel"), ESetPixel),
    ENTRY(TEXT("StretchBlt"), EStretchBlt),
    ENTRY(TEXT("Transparent Image"), ETransparentImage),
    ENTRY(TEXT("StretchDIBits"), EStretchDIBits),
    ENTRY(TEXT("SetDIBitsToDevice"), ESetDIBitsToDevice),
    ENTRY(TEXT("GradientFill"), EGradientFill),
    ENTRY(TEXT("InvertRect"), EInvertRect),
    ENTRY(TEXT("MoveToEx"), EMoveToEx),
    ENTRY(TEXT("LineTo"), ELineTo),
    ENTRY(TEXT("GetDIBColorTable"), EGetDIBColorTable),
    ENTRY(TEXT("SetDIBColorTAble"), ESetDIBColorTable),
    
    // brush & pens
    ENTRY(TEXT("CreateDIBPatternBrushPt"), ECreateDIBPatternBrushPt),
    ENTRY(TEXT("CreatePatternBrush"), ECreatePatternBrush),
    ENTRY(TEXT("CreatSolidBrush"), ECreateSolidBrush),
    ENTRY(TEXT("GetBrushOrgEx"), EGetBrushOrgEx),
    ENTRY(TEXT("GetSysColorBrush"), EGetSysColorBrush),
    ENTRY(TEXT("SetBrushOrgEx"), ESetBrushOrgEx),
    ENTRY(TEXT("CreatePenIndirect"), ECreatePenIndirect),
    ENTRY(TEXT("GetStockBrush"), EGetStockObject),
    // dc
    ENTRY(TEXT("CreateCompatibleDC"), ECreateCompatibleDC),
    ENTRY(TEXT("DeleteDC"), EDeleteDC),
    ENTRY(TEXT("GetDCOrgEx"), EGetDCOrgEx),
    ENTRY(TEXT("GetDeviceCaps"), EGetDeviceCaps),
    ENTRY(TEXT("RestoreDC"), ERestoreDC),
    ENTRY(TEXT("SaveDC"), ESaveDC),
    ENTRY(TEXT("ScrollDC"), EScrollDC),
    // do
    ENTRY(TEXT("DeleteObject"), EDeleteObject),
    ENTRY(TEXT("GetCurrentObject"), EGetCurrentObject),
    ENTRY(TEXT("GetObject"), EGetObject),
    ENTRY(TEXT("GetObjectType"), EGetObjectType),
    ENTRY(TEXT("GetStockObject"), EGetStockObject),
    ENTRY(TEXT("SelectObject"), ESelectObject),
    // text
    ENTRY(TEXT("DrawTextW"), EDrawTextW),
    ENTRY(TEXT("ExtTextOut"), EExtTextOut),
    ENTRY(TEXT("GetTextAlign"), EGetTextAlign),
    ENTRY(TEXT("GetTextExtentPoint"), EGetTextExtentPoint),
    ENTRY(TEXT("GetTextExtentPoint32"), EGetTextExtentPoint32),
    ENTRY(TEXT("GetTextExtentExPoint"), EGetTextExtentExPoint),
    ENTRY(TEXT("GetTextFace"), EGetTextFace),
    ENTRY(TEXT("GetTextMetrics"), EGetTextMetrics),
    ENTRY(TEXT("SetTextAlign"), ESetTextAlign),
    // testchk
    ENTRY(TEXT("CheckAllWhite"), ECheckAllWhite),
    ENTRY(TEXT("CheckScreenHalves"), ECheckScreenHalves),
    ENTRY(TEXT("GetReleaseDC"), EGetReleaseDC),
    ENTRY(TEXT("Thrash"), EThrash),
    // Manual
    ENTRY(TEXT("Rop2"), ERop2),
    ENTRY(TEXT("ManualFont"), EManualFont),
    ENTRY(TEXT("ManualFontClip"), EManualFontClip),
    ENTRY(NULL, NULL)
};

#undef ENTRY

MapClass funcName(&funcEntryStruct[0]);
