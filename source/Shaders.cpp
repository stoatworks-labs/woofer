#include "Shaders.h"

namespace woofer::shaders
{

const char* const kVersion = "#version 410 core\n";

/// The full-screen triangle, from gl_VertexID alone.
const char* const kQuadVertex = R"(
void main()
{
	vec2 p = vec2( float( ( gl_VertexID << 1 ) & 2 ), float( gl_VertexID & 2 ) );
	gl_Position = vec4( p * 2.0 - 1.0, 0.0, 1.0 );
}
)";

/// sRGB both ways (IEC 61966-2-1), and mix() exact at its ends.
const char* const kCommon = R"(
vec3 toLinear( vec3 c )
{
	vec3 lo = c / 12.92;
	vec3 hi = pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) );
	return mix( lo, hi, step( vec3( 0.04045 ), c ) );
}
vec3 toCode( vec3 c )
{
	c       = clamp( c, 0.0, 1.0 );
	vec3 lo = c * 12.92;
	vec3 hi = 1.055 * pow( c, vec3( 1.0 / 2.4 ) ) - 0.055;
	return mix( lo, hi, step( vec3( 0.0031308 ), c ) );
}
//GLSL does not promise mix( x, y, 1.0 ) == y: Apple's software renderer's is
//a float's last bit off it (slitscan).
vec4 blend( vec4 x, vec4 y, float a )
{
	return a <= 0.0 ? x : a >= 1.0 ? y : mix( x, y, a );
}
)";

const char* const kWooferFragment = R"(
uniform sampler2D InputTexture;
uniform sampler2D Table;      //Bins x (2 RowStride): rows 0.. A, RowStride.. B
uniform vec2 MaxUV;
uniform ivec2 Size;
uniform ivec2 ViewOrigin;
uniform float RhoMax;         //the table's reach, frame heights
uniform int Bins;
uniform int Rows;             //exposure instants
uniform int RowStride;        //where the B rows start
uniform float RayScale;       //r / ( D - z ) per frame height of screen radius
uniform vec3 LightDir;        //toward the light; x right, y up, z to the camera
uniform float Ambient;
uniform float GlossAlpha;     //GGX alpha
uniform float GlossAmount;
uniform int ViewIndex;        //0 Lit, 1 Height
uniform float HeightScale;    //code per metre of displacement, about 0.5
uniform vec4 LampColour;      //rgb (code), a: drawn
uniform vec2 LampPos;         //frame heights, centred
uniform float LampRadius;
uniform float MixAmount;
uniform int Hooks;

out vec4 fragColor;

const float kPi     = 3.14159265358979;
const float kF0     = 0.04;   //a dielectric: paper, polypropylene, anodised skin
const float kBaffle = 0.02;   //the bare baffle's reflectance
const int kHookAlphaPassthrough = 1;

//The table's own linear blend, in float, between bin centres -- and past
//the outermost centres, linear EXTRAPOLATION: the first and last half bin
//would otherwise hold the rest radius still while the pixel's radius moves,
//shifting the clip up to half a bin (a sixth of a pixel at 720p, a third of
//a code-step on a hard edge; wftest --rest).
vec4 tableAt( int row, float bins )
{
	float x = bins - 0.5;
	int i0  = clamp( int( floor( x ) ), 0, Bins - 2 );
	float f = x - float( i0 );
	vec4 a  = texelFetch( Table, ivec2( i0, row ), 0 );
	vec4 b  = texelFetch( Table, ivec2( i0 + 1, row ), 0 );
	return a + ( b - a ) * f;
}

void main()
{
	vec2 frag    = gl_FragCoord.xy - vec2( ViewOrigin );
	vec2 uv      = frag / vec2( Size );
	vec4 src     = texture( InputTexture, uv * MaxUV );
	float aspect = float( Size.x ) / float( Size.y );
	vec2 centre  = vec2( 0.5 * aspect, 0.5 );
	vec2 p       = frag / float( Size.y ) - centre;
	float rho    = length( p );
	vec2 dir     = rho > 0.0 ? p / rho : vec2( 1.0, 0.0 );

	//Diffuse normalised so a surface facing the camera returns its albedo.
	float lit = 1.0 / ( Ambient + ( 1.0 - Ambient ) * max( LightDir.z, 0.0 ) );

	vec3 shown;
	if( rho >= RhoMax )
	{
		vec3 albedo = mix( vec3( kBaffle ), toLinear( src.rgb ), src.a );
		shown       = ViewIndex == 1 ? vec3( 0.5 ) : toCode( albedo );
	}
	else
	{
		float bins = rho / RhoMax * float( Bins );
		vec3 view  = normalize( vec3( -rho * RayScale * dir, 1.0 ) );
		vec3 sum   = vec3( 0.0 );
		float lift = 0.0;
		for( int k = 0; k < Rows; ++k )
		{
			vec4 a      = tableAt( k, bins );
			vec4 b      = tableAt( k + RowStride, bins );
			a.w         = clamp( a.w, 0.0, 1.0 );
			b.xy        = max( b.xy, vec2( 0.0 ) );
			//Where this material was printed from: its rest screen radius,
			//on the pixel's own azimuth.
			vec2 from   = centre + a.x * dir;
			vec4 clip   = texture( InputTexture, from / vec2( aspect, 1.0 ) * MaxUV );
			vec3 albedo = mix( vec3( b.x ), toLinear( clip.rgb ), clip.a * a.w );
			vec3 n      = normalize( vec3( a.y * dir, a.z ) );
			float nl    = max( dot( n, LightDir ), 0.0 );
			vec3 light  = albedo * ( Ambient + ( 1.0 - Ambient ) * nl );
			if( b.y > 0.0 && GlossAmount > 0.0 && nl > 0.0 )
			{
				//GGX, height-correlated Smith, Schlick: throw's, f times pi.
				vec3 hv     = normalize( LightDir + view );
				float nv    = max( dot( n, view ), 1e-4 );
				float nh    = max( dot( n, hv ), 0.0 );
				float vh    = clamp( dot( view, hv ), 0.0, 1.0 );
				float a2    = GlossAlpha * GlossAlpha;
				float dd    = nh * nh * ( a2 - 1.0 ) + 1.0;
				float D     = a2 / ( kPi * dd * dd );
				float smith = 0.5 / ( nl * sqrt( nv * nv * ( 1.0 - a2 ) + a2 ) + nv * sqrt( nl * nl * ( 1.0 - a2 ) + a2 ) );
				float m     = 1.0 - vh;
				float F     = kF0 + ( 1.0 - kF0 ) * ( m * m * m * m * m );
				light += vec3( b.y * GlossAmount * ( 1.0 - Ambient ) * kPi * D * smith * F * nl );
			}
			sum += light;
			lift += b.z;
		}
		sum  = sum * ( lit / float( Rows ) );
		lift = lift / float( Rows );
		shown = ViewIndex == 1 ? vec3( 0.5 + lift * HeightScale ) : toCode( sum );
	}

	//The status lamp: an LED on the baffle, a pixel of antialiasing.
	if( ViewIndex == 0 && LampColour.a > 0.0 )
	{
		float d   = length( p - LampPos ) * float( Size.y );
		float cov = clamp( LampRadius * float( Size.y ) - d + 0.5, 0.0, 1.0 );
		shown     = mix( shown, LampColour.rgb, cov * LampColour.a );
	}

	fragColor = blend( src, vec4( shown, 1.0 ), MixAmount );
	if( ( Hooks & kHookAlphaPassthrough ) != 0 )
		fragColor.a = src.a;
}
)";

std::string AssembleVertex()
{
	return std::string( kVersion ) + kQuadVertex;
}

std::string AssembleFragment()
{
	return std::string( kVersion ) + kCommon + kWooferFragment;
}

} // namespace woofer::shaders
