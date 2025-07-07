#include "std.h"
#include "gxgraphics.h"
#include "gxruntime.h"
#include "../gxruntime/gxutf8.h"

extern gxRuntime* gx_runtime;
static Debugger* debugger;

gxGraphics::gxGraphics(gxRuntime* rt, IDirect3D9* dd, IDirect3DSurface9* fs, IDirect3DSurface9* bs, bool d3d) :
	runtime(rt), dirDraw(dd), dir3d(0), dir3dDev(0), gfx_lost(false), dummy_mesh(0) {
	dirDraw->QueryInterface(IID_IDirectDraw, (void**)&ds_dirDraw);

	front_canvas = new gxCanvas(this, fs, 0);
	back_canvas = new gxCanvas(this, bs, 0);

	front_canvas->cls();
	back_canvas->cls();

	FT_Init_FreeType(&ftLibrary);

	HMODULE ntdllModule = GetModuleHandleW(L"ntdll.dll");
	running_on_wine = ntdllModule && GetProcAddress(ntdllModule, "wine_get_version");

	def_font = running_on_wine ? nullptr : this->loadFont(UTF8::getSystemFontFile("Courier"), 12);

	front_canvas->setFont(def_font);
	back_canvas->setFont(def_font);

	memset(&primFmt, 0, sizeof(primFmt));
	primFmt.dwSize = sizeof(primFmt);
	fs->GetPixelFormat(&primFmt);

	getTotalVidmem();

	//are we fullscreen?
	_gamma = 0;
	if (fs != bs) {
		if (fs->QueryInterface(IID_IDirectDrawGammaControl, (void**)&_gamma) >= 0) {
			if (_gamma->GetGammaRamp(0, &_gammaRamp) < 0) _gamma = 0;
		}
	}
	if (!_gamma) {
		for (int k = 0; k < 256; ++k) _gammaRamp.red[k] = _gammaRamp.blue[k] = _gammaRamp.green[k] = k;
	}
}

gxGraphics::~gxGraphics() {
	if (_gamma) _gamma->Release();
	while (scene_set.size()) freeScene(*scene_set.begin());
	while (movie_set.size()) closeMovie(*movie_set.begin());
	while (font_set.size()) freeFont(*font_set.begin());
	while (canvas_set.size()) freeCanvas(*canvas_set.begin());
	/*
	std::set<std::set<std::any>*>::iterator custom_set_it;
	for (custom_set_it = custom_set.begin(); custom_set_it != custom_set.end(); ++custom_set_it) {
		while ((*custom_set_it)->size()) {
			(*custom_set_it)->erase((*custom_set_it)->begin());
			delete* custom_set_it;
		}
	}
	*/

	std::set<std::string>::iterator it;
	for (it = font_res.begin(); it != font_res.end(); ++it) RemoveFontResource((*it).c_str());
	font_res.clear();

	delete back_canvas;
	delete front_canvas;

	FT_Done_FreeType(ftLibrary);

	ds_dirDraw->Release();

	dirDraw->RestoreDisplayMode();
	dirDraw->Release();
}




bool gxGraphics::restore() {

	while (dirDraw->TestCooperativeLevel() != DD_OK) {

		if (dirDraw->TestCooperativeLevel() == DDERR_WRONGMODE) return false;

		Sleep(100);
	}

	if (back_canvas->getSurface()->IsLost() == DD_OK) return true;

	dirDraw->RestoreAllSurfaces();

	//restore all canvases
	std::set<gxCanvas*>::iterator it;
	for (it = canvas_set.begin(); it != canvas_set.end(); ++it) {
		(*it)->restore();
	}

	//restore all meshes (b3d surfaces)
	std::set<gxMesh*>::iterator mesh_it;
	for (mesh_it = mesh_set.begin(); mesh_it != mesh_set.end(); ++mesh_it) {
		(*mesh_it)->restore();
	}
	if (dir3d) dir3dDev->EvictManagedResources();

	return true;
}

gxCanvas* gxGraphics::getFrontCanvas()const {
	return front_canvas;
}

gxCanvas* gxGraphics::getBackCanvas()const {
	return back_canvas;
}

gxFont* gxGraphics::getDefaultFont()const {
	return def_font;
}

void gxGraphics::vwait() {
	dirDraw->WaitForVerticalBlank(DDWAITVB_BLOCKBEGIN, 0);
}

void gxGraphics::flip(bool v) {
	runtime->flip(v);
}

void gxGraphics::copy(gxCanvas* dest, int dx, int dy, int dw, int dh, gxCanvas* src, int sx, int sy, int sw, int sh) {
	RECT r = { dx,dy,dx + dw,dy + dh };
	ddUtil::copy(dest->getSurface(), dx, dy, dw, dh, src->getSurface(), sx, sy, sw, sh);
	dest->damage(r);
}

int gxGraphics::getScanLine()const {
	DWORD t = 0;
	dirDraw->GetScanLine(&t);
	return t;
}

int gxGraphics::getTotalVidmem()const {
	DDCAPS caps = { sizeof(caps) };
	dirDraw->GetCaps(&caps, 0);
	return caps.dwVidMemTotal / 1024;
}

int gxGraphics::getAvailVidmem()const {
	DDCAPS caps = { sizeof(caps) };
	dirDraw->GetCaps(&caps, 0);
	return caps.dwVidMemFree / 1024;
}

gxMovie* gxGraphics::openMovie(const std::string& file, int flags) {

	IAMMultiMediaStream* iam_stream;

	if (CoCreateInstance(
		CLSID_AMMultiMediaStream, NULL, CLSCTX_INPROC_SERVER,
		IID_IAMMultiMediaStream, (void**)&iam_stream) == S_OK) {

		if (iam_stream->Initialize(STREAMTYPE_READ, AMMSF_NOGRAPHTHREAD, NULL) == S_OK) {

			if (iam_stream->AddMediaStream(ds_dirDraw, &MSPID_PrimaryVideo, 0, 0) == S_OK) {

				iam_stream->AddMediaStream(NULL, &MSPID_PrimaryAudio, AMMSF_ADDDEFAULTRENDERER, NULL);

				WCHAR* path = new WCHAR[file.size() + 1];
				MultiByteToWideChar(CP_ACP, 0, file.c_str(), -1, path, sizeof(WCHAR) * (file.size() + 1));
				int n = iam_stream->OpenFile(path, 0);
				delete path;

				if (n == S_OK) {
					gxMovie* movie = new gxMovie(this, iam_stream);
					movie->filename = file;
					movie_set.insert(movie);
					return movie;
				}
			}
		}
		iam_stream->Release();
	}
	return 0;
}

gxMovie* gxGraphics::verifyMovie(gxMovie* m) {
	return movie_set.count(m) ? m : 0;
}

void gxGraphics::closeMovie(gxMovie* m) {
	if (movie_set.erase(m)) delete m;
}

gxCanvas* gxGraphics::createCanvas(int w, int h, int flags) {
	ddSurf* s = ddUtil::createSurface(w, h, flags, this);
	if (!s) return 0;
	gxCanvas* c = new gxCanvas(this, s, flags);
	canvas_set.insert(c);
	c->cls();
	if (debugger) printf("Created new gxCanvas instance.");
	return c;
}

gxCanvas* gxGraphics::loadCanvas(const std::string& f, int flags) {
	ddSurf* s = ddUtil::loadSurface(f, flags, this);
	if (!s) return 0;
	gxCanvas* c = new gxCanvas(this, s, flags);
	canvas_set.insert(c);
	return c;
}

gxCanvas* gxGraphics::verifyCanvas(gxCanvas* c) {
	return canvas_set.count(c) || c == front_canvas || c == back_canvas ? c : 0;
}

void gxGraphics::freeCanvas(gxCanvas* c) {
	if (canvas_set.erase(c)) delete c;
}

int gxGraphics::getWidth()const {
	return front_canvas->getWidth();
}

int gxGraphics::getHeight()const {
	return front_canvas->getHeight();
}

int gxGraphics::getDepth()const {
	return front_canvas->getDepth();
}

gxFont* gxGraphics::loadFont(std::string f, int height, bool bold, bool italic, bool underlined) {
	std::string t;
	int n = f.find('.');
	if (n == std::string::npos) {
		t = fullfilename(f);
		if (!font_res.count(t) && AddFontResource(t.c_str())) font_res.insert(t);
		t = filenamefile(f.substr(0, n));
	}
	else {
		t = f;
	}

	gxFont* newFont = new gxFont(ftLibrary, this, f, height, bold, italic, underlined);
	font_set.emplace(newFont);
	return newFont;
}

gxFont* gxGraphics::verifyFont(gxFont* f) {
	return font_set.count(f) ? f : 0;
}

void gxGraphics::freeFont(gxFont* f) {
	if (font_set.erase(f)) delete f;
}

//////////////
// 3D STUFF //
//////////////

static int maxDevType;

static HRESULT CALLBACK enumDevice(D3DADAPTER_IDENTIFIER9* adapter, D3DDISPLAYMODE* displayMode, void* context) {
	gxGraphics* g = (gxGraphics*)context;
	D3DCAPS9 caps;
	g->dir3dDev->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps);

	int t = 0;
	if (caps.DeviceType == D3DDEVTYPE_REF) t = 1;
	else if (caps.DeviceType == D3DDEVTYPE_HAL) t = 2;
	else if (caps.DeviceType == D3DDEVTYPE_SW) t = 3;
	if (t > maxDevType) {
		g->dir3dDevDesc = caps;
		maxDevType = t;
	}
	return 1;
}

static HRESULT CALLBACK enumZbuffFormat(LPDDPIXELFORMAT format, void* context) {
	gxGraphics* g = (gxGraphics*)context;
	if (format->dwZBufferBitDepth == g->primFmt.dwRGBBitCount) {
		g->zbuffFmt = *format;
		return 0;
	}
	if (format->dwZBufferBitDepth > g->zbuffFmt.dwZBufferBitDepth) {
		if (format->dwZBufferBitDepth < g->primFmt.dwRGBBitCount) {
			g->zbuffFmt = *format;
		}
	}
	return 1;
}

struct TexFmt {
	D3DFORMAT fmt;
	int bits, a_bits, rgb_bits;
};

static int cntBits(int mask) {
	int n = 0;
	for (int k = 0; k < 32; ++k) {
		if (mask & (1 << k)) ++n;
	}
	return n;
}

static std::vector<TexFmt> tex_fmts;

static HRESULT CALLBACK enumTextureFormat(D3DFORMAT* fmt, void* p) {
	TexFmt t;
	t.fmt = *fmt;
	t.bits = fmt->dwRGBBitCount;
	t.a_bits = (fmt->dwFlags & DDPF_ALPHAPIXELS) ? cntBits(fmt->dwRGBAlphaBitMask) : 0;
	t.rgb_bits = (fmt->dwFlags & DDPF_RGB) ? cntBits(fmt->dwRBitMask | fmt->dwGBitMask | fmt->dwBBitMask) : 0;

	tex_fmts.push_back(t);

	return 1;
}

static std::string itobin(int n) {
	std::string t;
	for (int k = 0; k < 32; n <<= 1, ++k) {
		t += (n & 0x80000000) ? '1' : '0';
	}
	return t;
}

#ifdef BETA
static void debugPF(const D3DFORMAT& pf) {
	string t;
	t = "Bits:" + itoa(pf.dwRGBBitCount);
	gx_runtime->debugLog(t.c_str());
	t = "R Mask:" + itobin(pf.dwRBitMask);
	gx_runtime->debugLog(t.c_str());
	t = "G Mask:" + itobin(pf.dwGBitMask);
	gx_runtime->debugLog(t.c_str());
	t = "B Mask:" + itobin(pf.dwBBitMask);
	gx_runtime->debugLog(t.c_str());
	t = "A Mask:" + itobin(pf.dwRGBAlphaBitMask);
	gx_runtime->debugLog(t.c_str());
	}
#endif

static void pickTexFmts(gxGraphics* g, int hi) {
	//texRGBFmt.
	{
		int pick = -1, max = 0, bits;
		for (int d = g->primFmt.dwRGBBitCount; d <= 32; d += 8) {
			for (int k = 0; k < tex_fmts.size(); ++k) {
				const TexFmt& t = tex_fmts[k];
				if (t.bits > d || !t.rgb_bits || t.rgb_bits < max) continue;
				if (t.rgb_bits == max && t.bits >= bits) continue;
				pick = k; max = t.rgb_bits; bits = t.bits;
			}
			if (!hi && pick >= 0) break;
		}
		if (pick < 0) g->texRGBFmt[hi] = g->primFmt;
		else g->texRGBFmt[hi] = tex_fmts[pick].fmt;
	}
	//texAlphaFmt
	{
		int pick = -1, max = 0, bits;
		for (int d = g->primFmt.dwRGBBitCount; d <= 32; d += 8) {
			for (int k = 0; k < tex_fmts.size(); ++k) {
				const TexFmt& t = tex_fmts[k];
				if (t.bits > d || !t.a_bits || t.a_bits < max) continue;
				if (t.a_bits == max && t.bits >= bits) continue;
				pick = k; max = t.a_bits; bits = t.bits;
			}
			if (!hi && pick >= 0) break;
		}
		if (pick < 0) g->texAlphaFmt[hi] = g->primFmt;
		else g->texAlphaFmt[hi] = tex_fmts[pick].fmt;
	}
	//texRGBAlphaFmt
	{
		int pick = -1, a8rgb8 = -1, max = 0, bits;
		for (int d = g->primFmt.dwRGBBitCount; d <= 32; d += 8) {
			for (int k = 0; k < tex_fmts.size(); ++k) {
				const TexFmt& t = tex_fmts[k];
				if (t.a_bits == 8 && t.bits == 16) { a8rgb8 = k; continue; }
				if (t.bits > d || !t.a_bits || !t.rgb_bits || t.a_bits < max) continue;
				if (t.a_bits == max && t.bits >= bits) continue;
				pick = k; max = t.a_bits; bits = t.bits;
			}
			if (!hi && pick >= 0) break;
		}
		if (pick < 0) pick = a8rgb8;
		if (pick < 0) g->texRGBAlphaFmt[hi] = g->primFmt;
		else g->texRGBAlphaFmt[hi] = tex_fmts[pick].fmt;
	}
	//texRGBMaskFmt...
	{
		int pick = -1, max = 0, bits;
		for (int d = g->primFmt.dwRGBBitCount; d <= 32; d += 8) {
			for (int k = 0; k < tex_fmts.size(); ++k) {
				const TexFmt& t = tex_fmts[k];
				if (!t.a_bits || !t.rgb_bits || t.rgb_bits < max) continue;
				if (t.rgb_bits == max && t.bits >= bits) continue;
				pick = k; max = t.rgb_bits; bits = t.bits;
			}
			if (!hi && pick >= 0) break;
		}
		if (pick < 0) g->texRGBMaskFmt[hi] = g->primFmt;
		else g->texRGBMaskFmt[hi] = tex_fmts[pick].fmt;
	}
}

gxScene* gxGraphics::createScene(int flags) {
	if (scene_set.size()) return 0;

	//get d3d
	if (dirDraw->QueryInterface(IID_IDirect3D9, (void**)&dir3d) >= 0) {
		//enum devices
		maxDevType = 0;
		if (dir3d->EnumDevices(enumDevice, this) >= 0 && maxDevType > 1) {
			//enum zbuffer formats
			zbuffFmt.dwZBufferBitDepth = 0;
			if (dir3d->EnumZBufferFormats(dir3dDevDesc.deviceGUID, enumZbuffFormat, this) >= 0) {
				//create zbuff for back buffer
				if (back_canvas->attachZBuffer()) {
					//create 3d device
					if (dir3d->CreateDevice(dir3dDevDesc.deviceGUID, back_canvas->getSurface(), &dir3d) >= 0) {
						//enum texture formats
						tex_fmts.clear();
						if (dir3d->CheckDeviceFormat(enumTextureFormat, this) >= 0) {
							pickTexFmts(this, 0);
							pickTexFmts(this, 1);
							tex_fmts.clear();
#ifdef BETA
							gx_runtime->debugLog("Texture RGB format:");
							debugPF(texRGBFmt);
							gx_runtime->debugLog("Texture Alpha format:");
							debugPF(texAlphaFmt);
							gx_runtime->debugLog("Texture RGB Alpha format:");
							debugPF(texRGBAlphaFmt);
							gx_runtime->debugLog("Texture RGB Mask format:");
							debugPF(texRGBMaskFmt);
							gx_runtime->debugLog("Texture Primary format:");
							debugPF(primFmt);
							string ts = "ZBuffer Bit Depth:" + itoa(zbuffFmt.dwZBufferBitDepth);
							gx_runtime->debugLog(ts.c_str());
#endif
							gxScene* scene = new gxScene(this, back_canvas);
							scene_set.insert(scene);

							//dummy_mesh = createMesh(8, 12, 0);

							return scene;
					}
						dir3dDev->Release();
						dir3dDev = 0;
				}
					back_canvas->releaseZBuffer();
			}
		}
	}
		dir3d->Release();
		dir3d = 0;
}
	return 0;
}

gxScene* gxGraphics::verifyScene(gxScene* s) {
	return scene_set.count(s) ? s : 0;
}

void gxGraphics::freeScene(gxScene* scene) {
	if (!scene_set.erase(scene)) return;
	dummy_mesh = 0;
	while (mesh_set.size()) freeMesh(*mesh_set.begin());
	back_canvas->releaseZBuffer();
	if (dir3dDev) { dir3dDev->Release(); dir3dDev = 0; }
	if (dir3d) { dir3d->Release(); dir3d = 0; }
	delete scene;
}

gxMesh* gxGraphics::createMesh(int max_verts, int max_tris, int flags) {

	static const int VTXFMT =
		D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX2 |
		D3DFVF_TEXCOORDSIZE2(0) | D3DFVF_TEXCOORDSIZE2(1);

	UINT vertexSize = D3DXGetFVFVertexSize(VTXFMT);
	UINT bufferSize = max_verts * vertexSize;

	IDirect3DVertexBuffer9* buff;
	if (dir3dDev->CreateVertexBuffer(bufferSize, D3DUSAGE_WRITEONLY, VTXFMT, D3DPOOL_MANAGED, &buff, NULL) < 0) return 0;
	WORD* indices = new WORD[max_tris * 3];
	gxMesh* mesh = new gxMesh(this, buff, indices, max_verts, max_tris);
	mesh_set.insert(mesh);
	return mesh;
}

gxMesh* gxGraphics::verifyMesh(gxMesh* m) {
	return mesh_set.count(m) ? m : 0;
}

void gxGraphics::freeMesh(gxMesh* mesh) {
	if (mesh_set.erase(mesh)) delete mesh;
}