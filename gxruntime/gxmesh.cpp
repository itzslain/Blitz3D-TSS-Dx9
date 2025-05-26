#include "std.h"
#include "gxmesh.h"
#include "gxgraphics.h"

#include "gxruntime.h"

extern gxRuntime* gx_runtime;

gxMesh::gxMesh(gxGraphics* g, IDirect3DVertexBuffer9* vs, WORD* is, int max_vs, int max_ts) :
	graphics(g), locked_verts(0), vertex_buff(vs), tri_indices(is), max_verts(max_vs), max_tris(max_ts), mesh_dirty(false), index_buff(0) {
	if (graphics && graphics->dir3dDev && tri_indices) {
		if (SUCCEEDED(graphics->dir3dDev->CreateIndexBuffer(max_tris * 3 * sizeof(WORD), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &index_buff, 0))) {
			void* pIndices;
			if (SUCCEEDED(index_buff->Lock(0, max_tris * 3 * sizeof(WORD), &pIndices, 0))) {
				memcpy(pIndices, tri_indices, max_tris * 3 * sizeof(WORD));
				index_buff->Unlock();
			}
		}
	}
}

gxMesh::~gxMesh() {
	unlock();

	if (vertex_buff) vertex_buff->Release();
	if (index_buff) index_buff->Release();

	delete[] tri_indices;
}

bool gxMesh::lock(bool all) {
	if (locked_verts) return true;

	//V1.1.06...
	DWORD flags = (all ? D3DLOCK_DISCARD : D3DLOCK_NOOVERWRITE);

	//XP or less?
	if (graphics->runtime->osinfo.dwMajorVersion < 6) {
		flags |= (all ? D3DLOCK_DISCARD : D3DLOCK_NOOVERWRITE);
	}

	if (SUCCEEDED(vertex_buff->Lock(0, 0, (void**)&locked_verts, flags))) {
		mesh_dirty = false;
		return true;
	}

	static dxVertex* err_verts = new dxVertex[32768];

	locked_verts = err_verts;
	return true;
}

void gxMesh::unlock() {
	if (locked_verts) {
		vertex_buff->Unlock();
		locked_verts = 0;
	}
}

void gxMesh::backup() {
	unlock();
}

void gxMesh::restore() {
	mesh_dirty = true;
}

void gxMesh::render(int first_vert, int vert_cnt, int first_tri, int tri_cnt) {
	unlock();

	if (mesh_dirty && index_buff) {
		void* pIndices;
		if (SUCCEEDED(index_buff->Lock(0, max_tris * 3 * sizeof(WORD), &pIndices, 0))) {
			memcpy(pIndices, tri_indices, max_tris * 3 * sizeof(WORD));
			index_buff->Unlock();
		}
		mesh_dirty = false;
	}

	graphics->dir3dDev->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX2);
	graphics->dir3dDev->SetStreamSource(0, vertex_buff, 0, sizeof(dxVertex));
	graphics->dir3dDev->SetIndices(index_buff);

	graphics->dir3dDev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, first_vert, first_vert, vert_cnt, first_tri * 3, tri_cnt);
}