#include "external/fenster.h" // https://github.com/zserge/fenster

#define TINYBVH_IMPLEMENTATION
#include "tiny_bvh.h"

using namespace tinybvh;

bvhvec4 triangles[259 /* level 3 */ * 6 * 2 * 49 * 3]{};
int verts = 0;
BVH bvh;

void sphere_flake( float x, float y, float z, float s, int d = 0 )
{
	// procedural tesselated sphere flake object
#define P(F,a,b,c) p[i+F*64]={(float)a ,(float)b,(float)c}
	bvhvec3 p[384], pos( x, y, z ), ofs( 3.5 );
	for (int i = 0, u = 0; u < 8; u++) for (int v = 0; v < 8; v++, i++)
		P( 0, u, v, 0 ), P( 1, u, 0, v ), P( 2, 0, u, v ),
		P( 3, u, v, 7 ), P( 4, u, 7, v ), P( 5, 7, u, v );
	for (int i = 0; i < 384; i++) p[i] = normalize( p[i] - ofs ) * s + pos;
	for (int i = 0, side = 0; side < 6; side++, i += 8)
		for (int u = 0; u < 7; u++, i++) for (int v = 0; v < 7; v++, i++)
			triangles[verts++] = p[i], triangles[verts++] = p[i + 8],
			triangles[verts++] = p[i + 1], triangles[verts++] = p[i + 1],
			triangles[verts++] = p[i + 9], triangles[verts++] = p[i + 8];
	if (d < 3) sphere_flake( x + s * 1.55f, y, z, s * 0.5f, d + 1 );
	if (d < 3) sphere_flake( x - s * 1.5f, y, z, s * 0.5f, d + 1 );
	if (d < 3) sphere_flake( x, y + s * 1.5f, z, s * 0.5f, d + 1 );
	if (d < 3) sphere_flake( x, x - s * 1.5f, z, s * 0.5f, d + 1 );
	if (d < 3) sphere_flake( x, y, z + s * 1.5f, s * 0.5f, d + 1 );
	if (d < 3) sphere_flake( x, y, z - s * 1.5f, s * 0.5f, d + 1 );
}

void Init()
{
	// generate a sphere flake scene
	sphere_flake( 0, 0, 0, 1.5f );

	// build a BVH over the scene
	bvh.Build( (bvhvec4*)triangles, verts / 3 );
}

void Tick( uint32_t* buf )
{
	// setup view pyramid for a pinhole camera: 
	// eye, p1 (top-left), p2 (top-right) and p3 (bottom-left)
	bvhvec3 eye( -3.5f, -1.5f, -6.5f ), view = normalize( bvhvec3( 3, 1.5f, 5 ) );
	bvhvec3 right = normalize( cross( bvhvec3( 0, 1, 0 ), view ) );
	bvhvec3 up = 0.8f * cross( view, right ), C = eye + 2 * view;
	bvhvec3 p1 = C - right + up, p2 = C + right + up, p3 = C - right - up;

	// generate primary rays in a cacheline-aligned buffer - and, for data locality:
	// organized in 4x4 pixel tiles, 16 samples per pixel, so 256 rays per tile.
	int N = 0;
	Ray* rays = (Ray*)ALIGNED_MALLOC( SCRWIDTH * SCRHEIGHT * 16 * sizeof( Ray ) );
	for( int ty = 0; ty < SCRHEIGHT / 4; ty++ ) for( int tx = 0; tx < SCRWIDTH / 4; tx++ )
	{
		for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++)
		{
			int pixel_x = tx * 4 + x;
			int pixel_y = ty * 4 + y;
			for (int s = 0; s < 16; s++) // 16 samples per pixel
			{
				float u = (float)(pixel_x * 4 + (s & 3)) / (SCRWIDTH * 4);
				float v = (float)(pixel_y * 4 + (s >> 2)) / (SCRHEIGHT * 4);
				bvhvec3 P = p1 + u * (p2 - p1) + v * (p3 - p1);
				rays[N++] = Ray( eye, normalize( P - eye ) );
			}
		}
	}
	
	// trace primary rays
#if 1
	const int packetCount = N / 256;
	for (int i = 0; i < packetCount; i++) bvh.Intersect256Rays( rays + i * 256 );
#else
	for (int i = 0; i < N; i++) bvh.Intersect( rays[i] );
#endif
	
	// visualize result
	for( int i = 0, ty = 0; ty < SCRHEIGHT / 4; ty++ ) for( int tx = 0; tx < SCRWIDTH / 4; tx++ )
	{
		for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++)
		{
			int pixel_x = tx * 4 + x;
			int pixel_y = ty * 4 + y;
			float avg = 0;
			for (int s = 0; s < 16; s++, i++) if (rays[i].hit.t < 1000)
			{
				int primIdx = rays[i].hit.prim;
				bvhvec3 v0 = triangles[primIdx * 3 + 0];
				bvhvec3 v1 = triangles[primIdx * 3 + 1];
				bvhvec3 v2 = triangles[primIdx * 3 + 2];
				bvhvec3 N = normalize( cross( v1 - v0, v2 - v0 ) );
				avg += fabs( dot( N, normalize( bvhvec3( 1, 2, 3 ) ) ) );
			}
			int c = (int)(15.9f * avg);
			buf[pixel_x + pixel_y * SCRWIDTH] = c + (c << 8) + (c << 16);
		}
	}
	ALIGNED_FREE(rays);
}
