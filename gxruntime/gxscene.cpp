#include "std.h"
#include "gxscene.h"
#include "gxgraphics.h"
#include "gxruntime.h"

static bool can_wb;
static int  hw_tex_stages, tex_stages;
static float BLACK[] = { 0,0,0 };
static float WHITE[] = { 1,1,1 };
static float GRAY[] = { .5f,.5f,.5f };
static D3DMATRIX sphere_mat, nullmatrix;

void gxScene::setRS(int n, int t) {
	if(d3d_rs[n] == t) return;
	dir3dDev->SetRenderState((D3DRENDERSTATETYPE)n, t);
	d3d_rs[n] = t;
}

void gxScene::setTSS(int n, int s, int t) {
	if(d3d_tss[n][s] == t) return;
	dir3dDev->SetTextureStageState(n, (D3DTEXTURESTAGESTATETYPE)s, t);
	d3d_tss[n][s] = t;
}

void gxScene::setSAMP(int n, D3DSAMPLERSTATETYPE s, int t) {
	if (d3d_samp[n][s] == t) return;
	dir3dDev->SetSamplerState(n, s, t);
	d3d_samp[n][s] = t;
}

gxScene::gxScene(gxGraphics* g, gxCanvas* t) :
	graphics(g), target(t), dir3dDev(g->dir3dDev),
	n_texs(0), tris_drawn(0) {

	memset(d3d_rs, 0x55, sizeof(d3d_rs));
	memset(d3d_tss, 0x55, sizeof(d3d_tss));
	memset(d3d_samp, 0x55, sizeof(d3d_samp));

	//nomalize normals
	setRS(D3DRS_NORMALIZENORMALS, TRUE);

	//vertex coloring
	setRS(D3DRS_COLORVERTEX, FALSE);
	setRS(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
	setRS(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_COLOR1);
	setRS(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);
	setRS(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);

	//Alpha test
	setRS(D3DRS_ALPHATESTENABLE, false);
	setRS(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
	setRS(D3DRS_ALPHAREF, 128);

	//source/dest blending modes
	setRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	setRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

	//suss out caps
	can_wb = false;
	hw_tex_stages = 1;
	D3DCAPS9 devDesc = { };
	if(dir3dDev->GetDeviceCaps(&devDesc) >= 0) {
		DWORD caps = devDesc.RasterCaps; // TODO: TriCaps replacement
		//texture stages
		hw_tex_stages = devDesc.MaxSimultaneousTextures;
		//depth buffer mode
		if((caps & D3DPRASTERCAPS_WBUFFER) && graphics->zbuffFmt == D3DFMT_D16) can_wb = true;
		//fog mode
		if((caps & D3DPRASTERCAPS_FOGTABLE) && (caps & D3DPRASTERCAPS_WFOG)) {
			setRS(D3DRS_FOGVERTEXMODE, D3DFOG_NONE);
			setRS(D3DRS_FOGTABLEMODE, D3DFOG_LINEAR);
		}
		else {
			setRS(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
			setRS(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
		}
	}
	tex_stages = hw_tex_stages;

	caps_level = 100;
	if(devDesc.TextureCaps & D3DPTEXTURECAPS_CUBEMAP) { //TODO: TriCaps replacement?
		caps_level = 110;
	}

	max_lights = devDesc.MaxActiveLights;

	//default texture states
	for(int n = 0; n < hw_tex_stages; ++n) {
		setTSS(n, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		setTSS(n, D3DTSS_COLORARG2, D3DTA_CURRENT);
		setTSS(n, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		setTSS(n, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
		setSAMP(n, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		setSAMP(n, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		setSAMP(n, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
	}
	setHWMultiTex(true);

	//globals
	sphere_mat._11 = .5f; sphere_mat._22 = -.5f; sphere_mat._33 = .5f;
	sphere_mat._41 = .5f; sphere_mat._42 = .5f; sphere_mat._43 = .5f;
	nullmatrix._11 = nullmatrix._22 = nullmatrix._33 = nullmatrix._44 = 1;

	//set null renderstate
	memset(&material, 0, sizeof(material));
	shininess = 0; blend = BLEND_REPLACE; fx = 0;
	for(int k = 0; k < MAX_TEXTURES; ++k) memset(&texstate[k], 0, sizeof(texstate[k]));

	wbuffer = can_wb;
	dither = false; setDither(true);
	antialias = true; setAntialias(false);
	wireframe = true; setWireframe(false);
	flipped = true; setFlippedTris(false);
	ambient = ~0; setAmbient(GRAY);
	ambient2 = ~0; setAmbient2(BLACK);
	fogcolor = ~0; setFogColor(BLACK);
	fogrange_nr = fogrange_fr = 0; setFogRange(1, 1000);
	fog_density = 0.0; setFogDensity(1.0);
	fogmode = FOG_LINEAR; setFogMode(FOG_NONE);
	zmode = -1; setZMode(ZMODE_NORMAL);
	memset(&projmatrix, 0, sizeof(projmatrix));
	ortho_proj = true; frustum_nr = frustum_fr = frustum_w = frustum_h = 0; setPerspProj(1, 1000, 1, 1);
	memset(&viewport, 0, sizeof(viewport)); viewport.MaxZ = 1; setViewport(0, 0, target->getWidth(), target->getHeight());
	viewmatrix = nullmatrix; setViewMatrix(0);
	worldmatrix = nullmatrix; setWorldMatrix(0);

	//set default renderstate
	blend = fx = ~0; shininess = 1;
	RenderState state; memset(&state, 0, sizeof(state));
	state.color[0] = state.color[1] = state.color[2] = state.alpha = 1;
	state.blend = BLEND_REPLACE;
	setRenderState(state);
}

gxScene::~gxScene() {
	while(_allLights.size()) freeLight(*_allLights.begin());
}

void gxScene::setTexState(int n, const TexState& state, bool tex_blend) {

	int flags = state.canvas->getFlags();
	int tc_index = state.flags & TEX_COORDS2 ? 1 : 0;

	IDirect3DSurface9* pSurface = state.canvas->getTexSurface();
	IDirect3DBaseTexture9* pTex;

	if (pSurface) {
		HRESULT hr = pSurface->GetContainer(IID_IDirect3DBaseTexture9, (void**)&pTex);
		if (SUCCEEDED(hr) && pTex) {
			dir3dDev->SetTexture(n, pTex);
			pTex->Release();
		}
		else {
			// Fallback
			dir3dDev->SetTexture(n, 0);
		}
	}
	else {
		// Fallback
		dir3dDev->SetTexture(n, 0);
	}

	//set addressing modes
	setSAMP(n, D3DSAMP_ADDRESSU, (flags & gxCanvas::CANVAS_TEX_CLAMPU) ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP);
	setSAMP(n, D3DSAMP_ADDRESSV, (flags & gxCanvas::CANVAS_TEX_CLAMPV) ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP);

	switch(flags & (
		gxCanvas::CANVAS_TEX_POINT |
		gxCanvas::CANVAS_TEX_BILINEAR |
		gxCanvas::CANVAS_TEX_NOFILTER |
		gxCanvas::CANVAS_TEX_ANISOTROPIC)) {
		case gxCanvas::CANVAS_TEX_POINT:
			setSAMP(n, D3DSAMP_MINFILTER, D3DTEXF_POINT);
			setSAMP(n, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
			setSAMP(n, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
			break;
		case gxCanvas::CANVAS_TEX_BILINEAR:
			setSAMP(n, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
			setSAMP(n, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
			setSAMP(n, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
			break;
		case gxCanvas::CANVAS_TEX_NOFILTER:
			setSAMP(n, D3DSAMP_MINFILTER, D3DTEXF_POINT);
			setSAMP(n, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
			setSAMP(n, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
			break;
		case gxCanvas::CANVAS_TEX_ANISOTROPIC:
			setSAMP(n, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC); //TODO: special sampler; something might be different here with this as the original 'D3DTFN_ANISOTROPIC'
			setSAMP(n, D3DSAMP_MAGFILTER, D3DTEXF_ANISOTROPIC);
			setSAMP(n, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
			setSAMP(n, D3DSAMP_MAXANISOTROPY, (textureAnisotropic > 0) ? textureAnisotropic : 1);
			break;
		default:
			setSAMP(n, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
			setSAMP(n, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
			setSAMP(n, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
	}

	//texgen
	switch(flags & (
		gxCanvas::CANVAS_TEX_SPHERE |
		gxCanvas::CANVAS_TEX_CUBE)) {

		case gxCanvas::CANVAS_TEX_SPHERE:
			setTSS(n, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACENORMAL);//|tc_index );
			setTSS(n, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
			dir3dDev->SetTransform((D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + n), &sphere_mat);
			break;
		case gxCanvas::CANVAS_TEX_CUBE:
			switch(state.canvas->cubeMode() & 3) {
				case gxCanvas::CUBEMODE_NORMAL:
					setTSS(n, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACENORMAL);//|tc_index );
					break;
				case gxCanvas::CUBEMODE_POSITION:
					setTSS(n, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);//|tc_index );
					break;
				default:
					setTSS(n, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);//|tc_index );
					break;
			}
			if(state.canvas->cubeMode() & 4) {
				setTSS(n, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
			}
			else {
				setTSS(n, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3);//COUNT4|D3DTTFF_PROJECTED );
				dir3dDev->SetTransform((D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + n), &inv_viewmatrix);
			}
			break;
		default:
			setTSS(n, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU | tc_index);
			if(state.mat_valid) {
				setTSS(n, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
				dir3dDev->SetTransform((D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + n), (D3DMATRIX*)&state.matrix);
			}
			else {
				setTSS(n, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
			}
	}

	if(!tex_blend) return;

	//blending
	switch(state.blend) {
		case BLEND_ALPHA:
			setTSS(n, D3DTSS_COLOROP, D3DTOP_BLENDTEXTUREALPHA);
			break;
		case BLEND_MULTIPLY:
			setTSS(n, D3DTSS_COLOROP, D3DTOP_MODULATE);
			break;
		case BLEND_ADD:
			setTSS(n, D3DTSS_COLOROP, D3DTOP_ADD);
			break;
		case BLEND_DOT3:
			setTSS(n, D3DTSS_COLOROP, D3DTOP_DOTPRODUCT3);
			break;
		case BLEND_MULTIPLY2:
			setTSS(n, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
			break;
		case BLEND_BUMPENVMAP:
			setTSS(n, D3DTSS_COLOROP, D3DTOP_BUMPENVMAP);
			break;
	}
	setTSS(n, D3DTSS_BUMPENVMAT00, state.bumpEnvMat[0][0]);
	setTSS(n, D3DTSS_BUMPENVMAT01, state.bumpEnvMat[0][1]);
	setTSS(n, D3DTSS_BUMPENVMAT10, state.bumpEnvMat[1][0]);
	setTSS(n, D3DTSS_BUMPENVMAT11, state.bumpEnvMat[1][1]);
	setTSS(n, D3DTSS_BUMPENVLSCALE, state.bumpEnvScale);
	setTSS(n, D3DTSS_BUMPENVLOFFSET, state.bumpEnvOffset);
	setTSS(n, D3DTSS_ALPHAOP, (flags & gxCanvas::CANVAS_TEX_ALPHA) ? D3DTOP_MODULATE : D3DTOP_SELECTARG2);
}

int  gxScene::hwTexUnits() {
	return tex_stages;
}

int  gxScene::gfxDriverCaps3D() {
	return caps_level;
}

void gxScene::setZMode() {
	switch(zmode) {
		case ZMODE_NORMAL:
			setRS(D3DRS_ZENABLE, wbuffer ? D3DZB_USEW : D3DZB_TRUE);
			setRS(D3DRS_ZWRITEENABLE, true);
			break;
		case ZMODE_DISABLE:
			setRS(D3DRS_ZENABLE, D3DZB_FALSE);
			setRS(D3DRS_ZWRITEENABLE, false);
			break;
		case ZMODE_CMPONLY:
			setRS(D3DRS_ZENABLE, wbuffer ? D3DZB_USEW : D3DZB_TRUE);
			setRS(D3DRS_ZWRITEENABLE, false);
			break;
	}
}

void gxScene::setLights() {
	if(fx & FX_FULLBRIGHT) {
		//no lights on
		for(int n = 0; n < _curLights.size(); ++n) dir3dDev->LightEnable(n, false);
	}
	else if(fx & FX_CONDLIGHT) {
		//some lights on
		for(int n = 0; n < _curLights.size(); ++n) {
			gxLight* light = _curLights[n];
			bool enable = light->d3d_light.Type != D3DLIGHT_DIRECTIONAL;
			dir3dDev->LightEnable(n, enable);
		}
	}
	else {
		//all lights on
		for(int n = 0; n < _curLights.size(); ++n) dir3dDev->LightEnable(n, true);
	}
}

void gxScene::setAmbient() {
	int n = (fx & FX_FULLBRIGHT) ? 0xffffff : ((fx & FX_CONDLIGHT) ? ambient2 : ambient);
	setRS(D3DRS_AMBIENT, n);
}

void gxScene::setFogMode() {
	if(!!(fx & FX_NOFOG)) {
		setRS(D3DRS_FOGENABLE, false);
		return;
	}
	switch(fogmode) {
		case FOG_NONE:
			setRS(D3DRS_FOGENABLE, false);
			break;
		case FOG_EXP:
			setRS(D3DRS_FOGENABLE, true);
			setRS(D3DRS_FOGTABLEMODE, D3DFOG_EXP);
			break;
		case FOG_EXP2:
			setRS(D3DRS_FOGENABLE, true);
			setRS(D3DRS_FOGTABLEMODE, D3DFOG_EXP2);
			break;
		case FOG_LINEAR:
			setRS(D3DRS_FOGENABLE, true);
			setRS(D3DRS_FOGTABLEMODE, D3DFOG_LINEAR);
			break;
	}
}

void gxScene::setTriCull() {
	if(fx & FX_DOUBLESIDED) {
		setRS(D3DRS_CULLMODE, D3DCULL_NONE);
	}
	else if(flipped) {
		setRS(D3DRS_CULLMODE, D3DCULL_CW);
	}
	else {
		setRS(D3DRS_CULLMODE, D3DCULL_CCW);
	}
}

void gxScene::setHWMultiTex(bool e) {
	for(int n = 0; n < 8; ++n) {
		setTSS(n, D3DTSS_COLOROP, D3DTOP_DISABLE);
		setTSS(n, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		dir3dDev->SetTexture(n, 0);
	}
	for(int k = 0; k < MAX_TEXTURES; ++k) {
		memset(&texstate[k], 0, sizeof(texstate[k]));
	}
	tex_stages = e ? hw_tex_stages : 1;
	n_texs = 0;
}

void gxScene::setWBuffer(bool n) {
	if(n == wbuffer || !can_wb) return;
	wbuffer = n; setZMode();
}

void gxScene::setDither(bool n) {
	if(n == dither) return;
	dither = n; setRS(D3DRS_DITHERENABLE, dither ? true : false);
}

// TODO: DX7 ANTIALIAS is likely superceded with MULTISAMPLEANTIALIAS. Will need to verify with that if it is the case.
// Fix: Replaced with True or False to apply the Antialias to render state.
void gxScene::setAntialias(bool n) {
	if(n == antialias) return;
	antialias = n; setRS(D3DRS_MULTISAMPLEANTIALIAS, antialias ? TRUE : FALSE);
}

void gxScene::setWireframe(bool n) {
	if(n == wireframe) return;
	wireframe = n;
}

void gxScene::setFlippedTris(bool n) {
	if(n == flipped) return;
	flipped = n; setTriCull();
}

void gxScene::setAmbient(const float rgb[]) {
	int n = (int(rgb[0] * 255.0f) << 16) | (int(rgb[1] * 255.0f) << 8) | int(rgb[2] * 255.0f);
	ambient = n; setAmbient();
}

void gxScene::setAmbient2(const float rgb[]) {
	int n = (int(rgb[0] * 255.0f) << 16) | (int(rgb[1] * 255.0f) << 8) | int(rgb[2] * 255.0f);
	ambient2 = n; setAmbient();
}

void gxScene::setViewport(int x, int y, int w, int h) {
	if(x == viewport.X && y == viewport.Y && w == viewport.Width && h == viewport.Height) return;
	viewport.X = x; viewport.Y = y; viewport.Width = w; viewport.Height = h;
	dir3dDev->SetViewport(&viewport);
}

void gxScene::setOrthoProj(float nr, float fr, float w, float h) {
	if(ortho_proj && nr == frustum_nr && fr == frustum_fr && w == frustum_w && h == frustum_h) return;
	frustum_nr = nr; frustum_fr = fr; frustum_w = w; frustum_h = h; ortho_proj = true;
	float W = 2 / w;
	float H = 2 / h;
	float Q = 1 / (fr - nr);
	projmatrix._11 = W;
	projmatrix._22 = H;
	projmatrix._33 = Q;
	projmatrix._34 = 0;
	projmatrix._43 = -Q * nr;
	projmatrix._44 = 1;
	dir3dDev->SetTransform(D3DTS_PROJECTION, &projmatrix);
}

void gxScene::setPerspProj(float nr, float fr, float w, float h) {
	if(!ortho_proj && nr == frustum_nr && fr == frustum_fr && w == frustum_w && h == frustum_h) return;
	frustum_nr = nr; frustum_fr = fr; frustum_w = w; frustum_h = h; ortho_proj = false;
	float W = 2 * nr / w;
	float H = 2 * nr / h;
	float Q = fr / (fr - nr);
	projmatrix._11 = W;
	projmatrix._22 = H;
	projmatrix._33 = Q;
	projmatrix._34 = 1;
	projmatrix._43 = -Q * nr;
	projmatrix._44 = 0;
	dir3dDev->SetTransform(D3DTS_PROJECTION, &projmatrix);
}

void gxScene::setFogColor(const float rgb[3]) {
	int n = (int(rgb[0] * 255.0f) << 16) | (int(rgb[1] * 255.0f) << 8) | int(rgb[2] * 255.0f);
	if(n == fogcolor) return;
	fogcolor = n; setRS(D3DRS_FOGCOLOR, fogcolor);
}

void gxScene::setFogRange(float nr, float fr) {
	if(nr == fogrange_nr && fr == fogrange_fr) return;
	fogrange_nr = nr; fogrange_fr = fr;
	setRS(D3DRS_FOGSTART, *(DWORD*)&fogrange_nr);
	setRS(D3DRS_FOGEND, *(DWORD*)&fogrange_fr);
}

void gxScene::setFogDensity(float den) {
	if(den == fog_density) return;
	fog_density = den;
	setRS(D3DRS_FOGDENSITY, *(DWORD*)&fog_density);
}

void gxScene::setFogMode(int n) {
	if(n == fogmode) return;
	fogmode = n; setFogMode();
}

void gxScene::setZMode(int n) {
	if(n == zmode) return;
	zmode = n; setZMode();
}

void gxScene::setViewMatrix(const Matrix* m) {
	if(m) {
		memcpy(&viewmatrix._11, m->elements[0], 12);
		memcpy(&viewmatrix._21, m->elements[1], 12);
		memcpy(&viewmatrix._31, m->elements[2], 12);
		memcpy(&viewmatrix._41, m->elements[3], 12);
		inv_viewmatrix._11 = viewmatrix._11; inv_viewmatrix._21 = viewmatrix._12; inv_viewmatrix._31 = viewmatrix._13;
		inv_viewmatrix._12 = viewmatrix._21; inv_viewmatrix._22 = viewmatrix._22; inv_viewmatrix._32 = viewmatrix._23;
		inv_viewmatrix._13 = viewmatrix._31; inv_viewmatrix._23 = viewmatrix._32; inv_viewmatrix._33 = viewmatrix._33;
		inv_viewmatrix._44 = viewmatrix._44;
	}
	else {
		viewmatrix = inv_viewmatrix = nullmatrix;
	}

	dir3dDev->SetTransform(D3DTS_VIEW, &viewmatrix);
}

void gxScene::setWorldMatrix(const Matrix* m) {
	if(m) {
		memcpy(&worldmatrix._11, m->elements[0], 12);
		memcpy(&worldmatrix._21, m->elements[1], 12);
		memcpy(&worldmatrix._31, m->elements[2], 12);
		memcpy(&worldmatrix._41, m->elements[3], 12);
	}
	else worldmatrix = nullmatrix;
	dir3dDev->SetTransform(D3DTS_WORLD, &worldmatrix);
}

void gxScene::setRenderState(const RenderState& rs) {
	bool setmat = false;
	if(memcmp(rs.color, &material.Diffuse.r, 12)) {
		memcpy(&material.Diffuse.r, rs.color, 12);
		memcpy(&material.Ambient.r, rs.color, 12);
		setmat = true;
	}
	if(rs.alpha != material.Diffuse.a) {
		material.Diffuse.a = rs.alpha;
		if(rs.fx & FX_ALPHATEST) {
			int alpharef = (rs.fx & FX_VERTEXALPHA) ? 0 : 128 * rs.alpha;
			setRS(D3DRS_ALPHAREF, alpharef);
		}
		setmat = true;
	}
	if(rs.shininess != shininess) {
		shininess = rs.shininess;
		float t = shininess > 0 ? (shininess < 1 ? shininess : 1) : 0;
		material.Specular.r = material.Specular.g = material.Specular.b = t;
		material.Power = shininess * 128;
		setRS(D3DRS_SPECULARENABLE, shininess > 0 ? true : false);
		setmat = true;
	}
	if(rs.blend != blend) {
		blend = rs.blend;
		switch(blend) {
			case BLEND_REPLACE:
				setRS(D3DRS_ALPHABLENDENABLE, false);
				break;
			case BLEND_ALPHA:
				setRS(D3DRS_ALPHABLENDENABLE, true);
				setRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
				setRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
				break;
			case BLEND_MULTIPLY:
				setRS(D3DRS_ALPHABLENDENABLE, true);
				setRS(D3DRS_SRCBLEND, D3DBLEND_DESTCOLOR);
				setRS(D3DRS_DESTBLEND, D3DBLEND_ZERO);
				break;
			case BLEND_ADD:
				setRS(D3DRS_ALPHABLENDENABLE, true);
				setRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
				setRS(D3DRS_DESTBLEND, D3DBLEND_ONE);
				break;
		}
	}
	if(rs.fx != fx) {
		int t = rs.fx ^ fx; fx = rs.fx;
		if(t & (FX_FULLBRIGHT | FX_CONDLIGHT)) {
			setLights();
			setAmbient();
		}
		if(t & FX_VERTEXCOLOR) {
			setRS(D3DRS_COLORVERTEX, fx & FX_VERTEXCOLOR ? true : false);
		}
		if(t & FX_FLATSHADED) {
			setRS(D3DRS_SHADEMODE, fx & FX_FLATSHADED ? D3DSHADE_FLAT : D3DSHADE_GOURAUD);
		}
		if(t & FX_NOFOG) {
			setFogMode();
		}
		if(t & FX_DOUBLESIDED) {
			setTriCull();
		}
		if(!wireframe && t & FX_WIREFRAME) {
			setRS(D3DRS_FILLMODE, fx & FX_WIREFRAME ? D3DFILL_WIREFRAME : D3DFILL_SOLID);
		}
		if(t & FX_EMISSIVE) {
			//Q3 Hack!
			int n = fx & FX_EMISSIVE;
			setRS(D3DRS_DIFFUSEMATERIALSOURCE, n ? D3DMCS_MATERIAL : D3DMCS_COLOR1);
			setRS(D3DRS_AMBIENTMATERIALSOURCE, n ? D3DMCS_MATERIAL : D3DMCS_COLOR1);
			setRS(D3DRS_EMISSIVEMATERIALSOURCE, n ? D3DMCS_COLOR1 : D3DMCS_MATERIAL);
			setRS(D3DRS_COLORVERTEX, n ? true : false);
		}
		if(t & FX_ALPHATEST) {
			if(fx & FX_ALPHATEST) {
				int alpharef = (rs.fx & FX_VERTEXALPHA) ? 0 : 128 * rs.alpha;
				setRS(D3DRS_ALPHAREF, alpharef);
			}
			setRS(D3DRS_ALPHATESTENABLE, fx & FX_ALPHATEST ? true : false);
		}
	}
	if(setmat) {
		dir3dDev->SetMaterial(&material);
	}

	n_texs = 0;
	TexState* hw = texstate;
	for(int k = 0; k < MAX_TEXTURES; ++k) {
		const RenderState::TexState& ts = rs.tex_states[k];
		if(!ts.canvas || !ts.blend) continue;
		bool settex = false;
		ts.canvas->getTexSurface();	//force mipmap rebuild
		if(ts.canvas != hw->canvas) { hw->canvas = ts.canvas; settex = true; }
		if(ts.blend != hw->blend) { hw->blend = ts.blend; settex = true; }
		if(ts.flags != hw->flags) { hw->flags = ts.flags; settex = true; }
		if(ts.bumpEnvMat[0][0] != hw->bumpEnvMat[0][0]) { hw->bumpEnvMat[0][0] = ts.bumpEnvMat[0][0]; settex = true; }
		if(ts.bumpEnvMat[1][0] != hw->bumpEnvMat[1][0]) { hw->bumpEnvMat[1][0] = ts.bumpEnvMat[1][0]; settex = true; }
		if(ts.bumpEnvMat[0][1] != hw->bumpEnvMat[0][1]) { hw->bumpEnvMat[0][1] = ts.bumpEnvMat[0][1]; settex = true; }
		if(ts.bumpEnvMat[1][1] != hw->bumpEnvMat[1][1]) { hw->bumpEnvMat[1][1] = ts.bumpEnvMat[1][1]; settex = true; }
		if(ts.bumpEnvScale != hw->bumpEnvScale) { hw->bumpEnvScale = ts.bumpEnvScale; settex = true; }
		if(ts.bumpEnvOffset != hw->bumpEnvOffset) { hw->bumpEnvOffset = ts.bumpEnvOffset; settex = true; }
		if(ts.matrix || hw->mat_valid) {
			if(ts.matrix) {
				memcpy(&hw->matrix._11, ts.matrix->elements[0], 12);
				memcpy(&hw->matrix._21, ts.matrix->elements[1], 12);
				memcpy(&hw->matrix._31, ts.matrix->elements[2], 12);
				memcpy(&hw->matrix._41, ts.matrix->elements[3], 12);
				hw->mat_valid = true;
			}
			else {
				hw->mat_valid = false;
			}
			settex = true;
		}
		if(settex && n_texs < tex_stages) {
			setTexState(n_texs, *hw, true);
		}
		++hw; ++n_texs;
	}
	if(n_texs < tex_stages && hw->canvas) {
		hw->canvas = 0;
		setTSS(n_texs, D3DTSS_COLOROP, D3DTOP_DISABLE);
		setTSS(n_texs, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		dir3dDev->SetTexture(n_texs, 0);
	}
}

bool gxScene::begin(const std::vector<gxLight*>& lights) {

	if(dir3dDev->BeginScene() != D3D_OK) return false;

	//clear textures!
	int n;
	for(n = 0; n < tex_stages; ++n) {
		texstate[n].canvas = 0;
		setTSS(n, D3DTSS_COLOROP, D3DTOP_DISABLE);
		setTSS(n, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		dir3dDev->SetTexture(n, 0);
		setSAMP(n, D3DSAMP_MIPMAPLODBIAS, textureLodBias);
	}

	//set light states
	_curLights.clear();
	for(n = 0; n < max_lights; ++n) {
		if(n < lights.size()) {
			_curLights.push_back(lights[n]);
			dir3dDev->SetLight(n, &_curLights[n]->d3d_light);
		}
		else {
			dir3dDev->LightEnable(n, false);
		}
	}
	setLights();

	setRS(D3DRS_FILLMODE, wireframe ? D3DFILL_WIREFRAME : D3DFILL_SOLID);

	return true;
}

void gxScene::clear(const float rgb[3], float alpha, float z, bool clear_argb, bool clear_z) {
	if(!clear_argb && !clear_z) return;
	int flags = (clear_argb ? D3DCLEAR_TARGET : 0) | (clear_z ? D3DCLEAR_ZBUFFER : 0);
	unsigned argb = (int(alpha * 255.0f) << 24) | (int(rgb[0] * 255.0f) << 16) | (int(rgb[1] * 255.0f) << 8) | int(rgb[2] * 255.0f);
	dir3dDev->Clear(0, 0, flags, argb, z, 0);
}

void gxScene::render(gxMesh* m, int first_vert, int vert_cnt, int first_tri, int tri_cnt) {

	m->render(first_vert, vert_cnt, first_tri, tri_cnt);
	tris_drawn += tri_cnt;
	if(n_texs <= tex_stages) return;

	setTSS(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	setTSS(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	if(tex_stages > 1) {
		setTSS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
		setTSS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	}

	setRS(D3DRS_LIGHTING, false);
	setRS(D3DRS_ALPHABLENDENABLE, true);

	for(int k = tex_stages; k < n_texs; ++k) {
		const TexState& state = texstate[k];
		switch(state.blend) {
			case BLEND_ALPHA:
				setRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
				setRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
				break;
			case BLEND_MULTIPLY:case BLEND_DOT3:
				setRS(D3DRS_SRCBLEND, D3DBLEND_DESTCOLOR);
				setRS(D3DRS_DESTBLEND, D3DBLEND_ZERO);
				break;
			case BLEND_ADD:
				setRS(D3DRS_SRCBLEND, D3DBLEND_ONE);
				setRS(D3DRS_DESTBLEND, D3DBLEND_ONE);
				break;
		}
		setTexState(0, state, false);
		m->render(first_vert, vert_cnt, first_tri, tri_cnt);
		tris_drawn += tri_cnt;
	}

	setRS(D3DRS_ALPHABLENDENABLE, false);
	setRS(D3DRS_LIGHTING, true);
	if(tex_stages > 1) setTexState(1, texstate[1], true);
	setTexState(0, texstate[0], true);
}

void gxScene::end() {
	dir3dDev->EndScene();
	RECT r = { viewport.X,viewport.Y,viewport.X + viewport.Width,viewport.Y + viewport.Height };
	target->damage(r);
}

gxLight* gxScene::createLight(int flags) {
	gxLight* l = new gxLight(this, flags);
	_allLights.insert(l);
	return l;
}

void gxScene::freeLight(gxLight* l) {
	_allLights.erase(l);
}

int gxScene::getTrianglesDrawn()const {
	return tris_drawn;
}