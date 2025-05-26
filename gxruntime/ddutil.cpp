#include "std.h"
#include "ddutil.h"
#include "asmcoder.h"
#include "gxcanvas.h"
#include "gxruntime.h"

extern gxRuntime* gx_runtime;

#include "..\freeimage\freeimage.h"

static AsmCoder asm_coder;

static void calcShifts(unsigned mask, unsigned char* shr, unsigned char* shl) {
	if (mask) {
		for (*shl = 0; !(mask & 1); ++*shl, mask >>= 1) {}
		for (*shr = 8; mask & 1; --*shr, mask >>= 1) {}
	}
	else *shr = *shl = 0;
}

PixelFormat::~PixelFormat() {
	if (plot_code) {
		VirtualFree(plot_code, 0, MEM_RELEASE);
	}
}

void PixelFormat::setFormat(const D3DFORMAT& pf) {
	if (plot_code) {
		VirtualFree(plot_code, 0, MEM_RELEASE);
	}

	plot_code = (char*)VirtualAlloc(0, 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	point_code = plot_code + 64;

	depth = 0;
	amask = rmask = gmask = bmask = 0;

	switch (pf) {
	case D3DFMT_A8R8G8B8:
		depth = 32;
		amask = 0xff000000;
		rmask = 0x00ff0000;
		gmask = 0x0000ff00;
		bmask = 0x000000ff;
		break;
	case D3DFMT_X8R8G8B8:
		depth = 32;
		amask = 0x00000000;
		rmask = 0x00ff0000;
		gmask = 0x0000ff00;
		bmask = 0x000000ff;
		break;
	case D3DFMT_R5G6B5:
		depth = 16;
		rmask = 0xf800;
		gmask = 0x07e0;
		bmask = 0x001f;
		break;
	case D3DFMT_A1R5G5B5:
		depth = 16;
		amask = 0x8000;
		rmask = 0x7C00;
		gmask = 0x03E0;
		bmask = 0x001F;
		break;
	case D3DFMT_A4R4G4B4:
		depth = 16;
		amask = 0xF000;
		rmask = 0x0F00;
		gmask = 0x00F0;
		bmask = 0x000F;
		break;
	case D3DFMT_X1R5G5B5:
		depth = 16;
		amask = 0x0000;
		rmask = 0x7C00;
		gmask = 0x03E0;
		bmask = 0x001F;
		break;
	case D3DFMT_R8G8B8:
		depth = 24;
		rmask = 0xFF0000;
		gmask = 0x00FF00;
		bmask = 0x0000FF;
		break;
	default:
		depth = 32;
		amask = 0xff000000;
		rmask = 0x00ff0000;
		gmask = 0x0000ff00;
		bmask = 0x000000ff;
		break;
	}
	pitch = depth / 8; argbfill = 0;
	if (!amask) argbfill |= 0xff000000;
	if (!rmask) argbfill |= 0x00ff0000;
	if (!gmask) argbfill |= 0x0000ff00;
	if (!bmask) argbfill |= 0x000000ff;
	calcShifts(amask, &ashr, &ashl); ashr += 24;
	calcShifts(rmask, &rshr, &rshl); rshr += 16;
	calcShifts(gmask, &gshr, &gshl); gshr += 8;
	calcShifts(bmask, &bshr, &bshl);
	plot = (Plot)(void*)plot_code;
	point = (Point)(void*)point_code;
	asm_coder.CodePlot(plot_code, depth, amask, rmask, gmask, bmask);
	asm_coder.CodePoint(point_code, depth, amask, rmask, gmask, bmask);
}

static void adjustTexSize(int* width, int* height, IDirect3DDevice9* dir3dDev) {
	D3DCAPS9 ddDesc;
	if (dir3dDev->GetDeviceCaps(&ddDesc) < 0) {
		*width = *height = 256;
		return;
	}
	int w = *width, h = *height, min, max;
	//make power of 2
	//Try *always* making POW2 size to fix GF6800 non-pow2 tex issue
	for (w = 1; w < *width; w <<= 1) {}
	for (h = 1; h < *height; h <<= 1) {}
	//make square
	if (ddDesc.TextureCaps & D3DPTEXTURECAPS_SQUAREONLY) {
		if (w > h) h = w;
		else w = h;
	}
	//check aspect ratio
	if (max = ddDesc.MaxTextureAspectRatio) {
		int asp = w > h ? w / h : h / w;
		if (asp > max) {
			if (w > h) h = w / max;
			else w = h / max;
		}
	}
	//clamp size
	// TODO: Check for replacement with Min Texture sizes is needed...
	//if((min = ddDesc.MinTextureWidth) && w < min) w = min;
	//if((min = ddDesc.MinTextureHeight) && h < min) h = min;
	if ((max = ddDesc.MaxTextureWidth) && w > max) w = max;
	if ((max = ddDesc.MaxTextureHeight) && h > max) h = max;

	*width = w; *height = h;
}

static ddSurf* createSurface(int width, int height, int pitch, void* bits, IDirect3DDevice9* dir3dDev) {
	ddSurf* surf = 0;
	HRESULT hr = dir3dDev->CreateOffscreenPlainSurface(width, height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &surf, 0);
	if (SUCCEEDED(hr)) {
		D3DLOCKED_RECT lr;
		if (SUCCEEDED(surf->LockRect(&lr, 0, 0))) {
			BYTE* src = (BYTE*)bits;
			BYTE* dst = (BYTE*)lr.pBits;
			for (int y = 0; y < height; ++y) {
				memcpy(dst, src, pitch);
				src += pitch;
				dst += lr.Pitch;
			}
			surf->UnlockRect();
		}
		else {
			surf->Release();
			surf = 0;
		}
	}
	return surf;
}

static void buildMask(ddSurf* surf) {
	D3DSURFACE_DESC desc;
	surf->GetDesc(&desc);
	D3DLOCKED_RECT lr;
	if (FAILED(surf->LockRect(&lr, 0, D3DLOCK_DISCARD))) return; // D3DLOCK_DISCARD likely a good substitute D3DLOCK_WAIT
	unsigned char* surf_p = (unsigned char*)lr.pBits;
	PixelFormat fmt(desc.Format);

	for (int y = 0; y < desc.Height; ++y) {
		unsigned char* p = surf_p;
		for (int x = 0; x < desc.Width; ++x) {
			unsigned argb = fmt.getPixel(p);
			unsigned rgb = argb & 0xffffff;
			unsigned a = rgb ? 0xff000000 : 0;
			fmt.setPixel(p, a | rgb);
			p += fmt.getPitch();
		}
		surf_p += lr.Pitch;
	}
	surf->UnlockRect();
}

static void buildAlpha(ddSurf* surf, bool whiten) {
	D3DSURFACE_DESC desc;
	surf->GetDesc(&desc);
	D3DLOCKED_RECT lr;
	if (FAILED(surf->LockRect(&lr, 0, D3DLOCK_DISCARD))) return;
	unsigned char* surf_p = (unsigned char*)lr.pBits;
	PixelFormat fmt(desc.Format);

	for (int y = 0; y < desc.Height; ++y) {
		unsigned char* p = surf_p;
		for (int x = 0; x < desc.Width; ++x) {
			unsigned argb = fmt.getPixel(p);
			unsigned alpha = (((argb >> 16) & 0xff) + ((argb >> 8) & 0xff) + (argb & 0xff)) / 3;
			argb = (alpha << 24) | (argb & 0xffffff);
			if (whiten) argb |= 0xffffff;
			fmt.setPixel(p, argb);
			p += fmt.getPitch();
		}
		surf_p += lr.Pitch;
	}
	surf->UnlockRect();
}

// TODO: I'm not sure if this is the best way to build mipmaps, or if we even should build them, but this is my best understanding of it.
// We can technically build them automatically using D3DUSAGE_AUTOGENMIPMAP before CreateTexture is called, so we need to see if there is 
// anything we should know about this if we do that instead of this. Another problem is that using D3DUSAGE_AUTOGENMIPMAP will only apply
// to parent textures. This code seem to invoke MipMap sublevel generations, we would need to use GenerateMipSubLevels should we need to 
// generate sublevels too, which is at the driver discretion. But for the point of this function, we will just manually generate a mipmap 
// for each parent texture's sublevel.
void ddUtil::buildMipMaps(ddSurf* surf) {

	IDirect3DTexture9* texture;
	if (FAILED(surf->GetContainer(IID_IDirect3DTexture9, (void**)&texture))) return;

	DWORD levels = texture->GetLevelCount();
	if (levels <= 1) { texture->Release(); return; }

	for (UINT level = 1; level < levels; level++) {
		IDirect3DSurface9* src_surf, * dest_surf;
		if (FAILED(texture->GetSurfaceLevel(level - 1, &src_surf)) ||
			FAILED(texture->GetSurfaceLevel(level, &dest_surf))) {
			if (src_surf) src_surf->Release();
			if (dest_surf) dest_surf->Release();
			continue;
		}

		// Get surface descriptions
		D3DSURFACE_DESC src_desc, dest_desc;
		src_surf->GetDesc(&src_desc);
		dest_surf->GetDesc(&dest_desc);

		// Lock both surfaces
		D3DLOCKED_RECT src_lock, dest_lock;
		if (FAILED(src_surf->LockRect(&src_lock, 0, 0))) {
			src_surf->Release();
			dest_surf->Release();
			continue;
		}
		if (FAILED(dest_surf->LockRect(&dest_lock, 0, 0))) {
			src_surf->UnlockRect();
			src_surf->Release();
			dest_surf->Release();
			continue;
		}

		PixelFormat src_format(src_desc.Format);
		PixelFormat dest_format(dest_desc.Format);

		unsigned char* src_bits = (unsigned char*)src_lock.pBits;
		unsigned char* dest_bits = (unsigned char*)dest_lock.pBits;

		int src_width = src_desc.Width;
		int src_height = src_desc.Height;
		int dest_width = dest_desc.Width;
		int dest_height = dest_desc.Height;

		// If the source is 1 pixel wide or high, do 1D downsampling.
		if (src_width == 1) {
			for (int y = 0; y < dest_height; y++) {
				unsigned p1 = src_format.getPixel(src_bits);
				unsigned p2 = src_format.getPixel(src_bits + src_lock.Pitch);
				unsigned argb = ((p1 & 0xfefefefe) >> 1) + ((p2 & 0xfefefefe) >> 1);
				argb += (((p1 & 0x01010101) + (p2 & 0x01010101)) >> 1) & 0x01010101;
				dest_format.setPixel(dest_bits, argb);
				src_bits += src_lock.Pitch * 2;
				dest_bits += dest_lock.Pitch;
			}
		}
		else if (src_height == 1) {
			for (int x = 0; x < dest_width; x++) {
				unsigned p1 = src_format.getPixel(src_bits);
				unsigned p2 = src_format.getPixel(src_bits + src_format.getPitch());
				unsigned argb = ((p1 & 0xfefefefe) >> 1) + ((p2 & 0xfefefefe) >> 1);
				argb += (((p1 & 0x01010101) + (p2 & 0x01010101)) >> 1) & 0x01010101;
				dest_format.setPixel(dest_bits, argb);
				src_bits += src_format.getPitch() * 2;
				dest_bits += dest_format.getPitch();
			}
		}
		else {
			// 2D downsampling: average 4 pixels into 1.
			for (int y = 0; y < dest_height; y++) {
				unsigned char* srcRow = src_bits;
				unsigned char* destRow = dest_bits;
				for (int x = 0; x < dest_width; x++) {
					unsigned p1 = src_format.getPixel(srcRow);
					unsigned p2 = src_format.getPixel(srcRow + src_format.getPitch());
					unsigned p3 = src_format.getPixel(srcRow + src_lock.Pitch);
					unsigned p4 = src_format.getPixel(srcRow + src_lock.Pitch + src_format.getPitch());

					unsigned argb = ((p1 & 0xfcfcfcfc) >> 2) + ((p2 & 0xfcfcfcfc) >> 2) + ((p3 & 0xfcfcfcfc) >> 2) + ((p4 & 0xfcfcfcfc) >> 2);
					argb += (((p1 & 0x03030303) + (p2 & 0x03030303) + (p3 & 0x03030303) + (p4 & 0x03030303)) >> 2) & 0x03030303;
					dest_format.setPixel(destRow, argb);
					srcRow += src_format.getPitch() * 2;
					destRow += dest_format.getPitch();
				}
				src_bits += src_lock.Pitch * 2;
				dest_bits += dest_lock.Pitch;
			}
		}

		dest_surf->UnlockRect();
		src_surf->UnlockRect();
		src_surf->Release();
		dest_surf->Release();
	}
	texture->Release();
}

void ddUtil::copy(ddSurf* dest, int dx, int dy, int dw, int dh, ddSurf* src, int sx, int sy, int sw, int sh) {

	D3DSURFACE_DESC src_desc, dest_desc;
	D3DLOCKED_RECT src_lr, dest_lr;
	src->GetDesc(&src_desc);
	if (FAILED(src->LockRect(&src_lr, 0, D3DLOCK_READONLY))) return;
	PixelFormat src_fmt(src_desc.Format);
	unsigned char* src_p = (unsigned char*)src_lr.pBits;
	src_p += src_lr.Pitch * sy + src_fmt.getPitch() * sx;

	dest->GetDesc(&dest_desc);
	if (FAILED(dest->LockRect(&dest_lr, 0, 0))) { src->UnlockRect(); return; }
	PixelFormat dest_fmt(dest_desc.Format);
	unsigned char* dest_p = (unsigned char*)dest_lr.pBits;
	dest_p += dest_lr.Pitch * dy + dest_fmt.getPitch() * dx;

	for (int y = 0; y < dh; ++y) {
		unsigned char* dest = dest_p;
		unsigned char* src = src_p + src_lr.Pitch * (y * sh / dh);
		for (int x = 0; x < dw; ++x) {
			dest_fmt.setPixel(dest, src_fmt.getPixel(src + src_fmt.getPitch() * (x * sw / dw)));
			dest += dest_fmt.getPitch();
		}
		dest_p += dest_lr.Pitch;
	}

	src->UnlockRect();
	dest->UnlockRect();
}

// TODO: Surface or Texture, who wants one?
ddSurf* ddUtil::createSurface(int w, int h, int flags, gxGraphics* gfx) {
	D3DFORMAT format;
	int hi = flags & gxCanvas::CANVAS_TEX_HICOLOR ? 1 : 0;

	if (flags & gxCanvas::CANVAS_TEX_MASK) {
		format = gfx->texRGBMaskFmt[hi];
	}
	else if (flags & gxCanvas::CANVAS_TEX_RGB) {
		format = (flags & gxCanvas::CANVAS_TEX_ALPHA) ? gfx->texRGBAlphaFmt[hi] : gfx->texRGBFmt[hi];
	}
	else if (flags & gxCanvas::CANVAS_TEX_ALPHA) {
		format = gfx->texAlphaFmt[hi];
	}
	else if (flags & gxCanvas::CANVAS_TEXTURE) {
		format = gfx->primFmt;
	}
	IDirect3DSurface9* surf;
	if (flags & gxCanvas::CANVAS_TEXTURE) {
		adjustTexSize(&w, &h, gfx->dir3dDev);
		DWORD usage = 0;
		D3DPOOL pool = (flags & gxCanvas::CANVAS_TEX_VIDMEM) ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED;
		int mip_levels = (flags & gxCanvas::CANVAS_TEX_MIPMAP) ? 0 : 1;

		/*if (flags & gxCanvas::CANVAS_TEX_MIPMAP) {
			mip_levels = 0;
			usage |= D3DUSAGE_AUTOGENMIPMAP;
		}*/

		IDirect3DTexture9* texture;
		HRESULT hr = gfx->dir3dDev->CreateTexture(w, h, mip_levels, usage, format, pool, &texture, 0);
		if (SUCCEEDED(hr)) {
			texture->GetSurfaceLevel(0, &surf);
			texture->Release();
			return surf;
		}
	}
	else {
		HRESULT hr = gfx->dir3dDev->CreateOffscreenPlainSurface(w, h, format, D3DPOOL_SYSTEMMEM, &surf, 0);
		if (SUCCEEDED(hr)) return surf;
	}
	return 0;
}

//Tom Speed's DXTC loader
//
// Should be good enough..?
IDirect3DSurface9* loadDXTC(const char* filename, gxGraphics* gfx) {
	IDirect3DTexture9* texture;
	HRESULT hr = D3DXCreateTextureFromFile(gfx->dir3dDev, filename, &texture);
	if (FAILED(hr) || !texture) return 0;
	IDirect3DSurface9* surf;
	hr = texture->GetSurfaceLevel(0, &surf);
	texture->Release();
	if (FAILED(hr)) return 0;
	return surf;
}

ddSurf* ddUtil::loadSurface(const std::string& f, int flags, gxGraphics* gfx) {

	int i = f.find(".dds");
	if (i != std::string::npos && i + 4 == f.size()) {
		//dds file!
		ddSurf* surf = loadDXTC(f.c_str(), gfx);
		return surf;
	}

	FreeImage_Initialise(true);
	FREE_IMAGE_FORMAT fmt = FreeImage_GetFileType(f.c_str(), f.size());
	if (fmt == FIF_UNKNOWN) {
		int n = f.find("."); if (n == std::string::npos) return 0;
		fmt = FreeImage_GetFileTypeFromExt(f.substr(n + 1).c_str());
		if (fmt == FIF_UNKNOWN) return 0;
	}
	FIBITMAP* t_dib = FreeImage_Load(fmt, f.c_str(), 0);
	if (!t_dib) return 0;

	bool trans = FreeImage_GetBPP(t_dib) == 32 || FreeImage_IsTransparent(t_dib);

	FIBITMAP* dib = FreeImage_ConvertTo32Bits(t_dib);

	if (dib) FreeImage_Unload(t_dib);
	else dib = t_dib;

	int width = FreeImage_GetWidth(dib);
	int height = FreeImage_GetHeight(dib);
	int pitch = FreeImage_GetPitch(dib);
	void* bits = FreeImage_GetBits(dib);

	ddSurf* src = ::createSurface(width, height, pitch, bits, gfx->dir3dDev);
	if (!src) {
		FreeImage_Unload(dib);
		return 0;
	}

	if (flags & gxCanvas::CANVAS_TEX_ALPHA) {
		if (flags & gxCanvas::CANVAS_TEX_MASK) {
			buildMask(src);
		}
		else if (!trans) {
			buildAlpha(src, (flags & gxCanvas::CANVAS_TEX_RGB) ? false : true);
		}
	}
	else {
		unsigned char* p = (unsigned char*)bits;
		for (int k = 0; k < height; ++k) {
			unsigned char* t = p + 3;
			for (int j = 0; j < width; ++j) {
				*t = 0xff; t += 4;
			}
			p += pitch;
		}
	}

	ddSurf* dest = createSurface(width, height, flags, gfx);
	if (!dest) {
		src->Release();
		FreeImage_Unload(dib);
		return 0;
	}

	int t_w = width, t_h = height;
	if (flags & gxCanvas::CANVAS_TEXTURE) adjustTexSize(&t_w, &t_h, gfx->dir3dDev);
	copy(dest, 0, 0, t_w, t_h, src, 0, height - 1, width, -height);

	src->Release();
	FreeImage_Unload(dib);
	FreeImage_DeInitialise();
	return dest;
}