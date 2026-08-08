#include <cmath>
#include <algorithm>
#include "Vector4f.h"
#include "Vector3f.h"
#include "Vector2f.h"

namespace Dark
{
	namespace Math
	{
const Vect4f Vect4f::ZERO	= { 0.0f, 0.0f, 0.0f, 0.0f };
const Vect4f Vect4f::ONE	= { 1.0f, 1.0f, 1.0f, 1.0f };

const Vect4f Vect4f::X_AXIS = { 1.0f, 0.0f, 0.0f, 1.0f };
const Vect4f Vect4f::Y_AXIS = { 0.0f, 1.0f, 0.0f, 1.0f };
const Vect4f Vect4f::Z_AXIS = { 0.0f, 0.0f, 1.0f, 1.0f };
const Vect4f Vect4f::W_AXIS = { 0.0f, 0.0f, 0.0f, 1.0f };

const Vect4f Vect4f::BLACK	= { 0.0f, 0.0f, 0.0f, 1.0f };
const Vect4f Vect4f::RED	= { 1.0f, 0.0f, 0.0f, 1.0f };
const Vect4f Vect4f::GREEN	= { 0.0f, 1.0f, 0.0f, 1.0f };
const Vect4f Vect4f::BLUE	= { 0.0f, 0.0f, 1.0f, 1.0f };
const Vect4f Vect4f::WHITE	= { 1.0f, 1.0f, 1.0f, 1.0f };

Vector4f::Vector4f( )
{
}

Vector4f::Vector4f( float X, float Y, float Z, float W )
{
	x = X;
	y = Y;
	z = Z;
	w = W;
}

Vector4f::Vector4f( const Vector3f& Vector, float W )
{
    x = Vector.x;
    y = Vector.y;
    z = Vector.z;
    w = W;
}

Vector4f::Vector4f( const Vector4f& Vector ) noexcept
{
	x = Vector.x;
	y = Vector.y;
	z = Vector.z;
	w = Vector.w;
}

Vector4f::Vector4f(const Vect4f& Vector)
{
	x = Vector.x;
	y = Vector.y;
	z = Vector.z;
	w = Vector.w;
}

Vector4f& Vector4f::operator= ( const Vector4f& Vector ) noexcept
{
	x = Vector.x;
	y = Vector.y;
	z = Vector.z;
	w = Vector.w;

    return *this;
}

Vector4f& Vector4f::operator= (const Vect4f& Vector)
{
	x = Vector.x;
	y = Vector.y;
	z = Vector.z;
	w = Vector.w;

	return *this;
}

void Vector4f::Normalize()
{
	float fInvMag = ( 1.0f / Magnitude() );

	x *= fInvMag;
	y *= fInvMag;
	z *= fInvMag;
	w *= fInvMag;
}

float Vector4f::Magnitude() const
{
	float fLength = 0.0f;

	fLength += x * x;
	fLength += y * y;
	fLength += z * z;
	fLength += w * w;

	return( sqrtf(fLength) );
}

float Vector4f::Dot( const Vector4f& Vector ) const
{
	float ret = 0.0f;
	
	ret += x * Vector.x;
	ret += y * Vector.y;
	ret += z * Vector.z;
	ret += w * Vector.w;

	return ret;
}

bool Vector4f::IsNaN() const
{
	return std::isnan(x) || std::isnan(y) || std::isnan(z) || std::isnan(w);
}

float Vector4f::operator[] ( int iPos ) const
{
	if ( iPos == 0 ) return( x );
	if ( iPos == 1 ) return( y );
	if ( iPos == 2 ) return( z );
	return( w );
}

float& Vector4f::operator[] ( int iPos )
{
	if ( iPos == 0 ) return( x );
	if ( iPos == 1 ) return( y );
	if ( iPos == 2 ) return( z );
	return( w );
}

bool Vector4f::operator== ( const Vector4f& Vector ) const
{
	if ( ( x - Vector.x ) * ( x - Vector.x ) > 0.01f )
		return false;
	if ( ( y - Vector.y ) * ( y - Vector.y ) > 0.01f )
		return false;
	if ( ( z - Vector.z ) * ( z - Vector.z ) > 0.01f )
		return false;
	if ( ( w - Vector.w ) * ( w - Vector.w ) > 0.01f )
		return false;

	return true;
}

bool Vector4f::operator!= ( const Vector4f& Vector ) const
{
    return( !( *this == Vector ) );
}

Vector4f Vector4f::operator+ ( const Vector4f& Vector ) const
{
	Vector4f sum;

	sum.x = x + Vector.x;
	sum.y = y + Vector.y;
	sum.z = z + Vector.z;
	sum.w = w + Vector.w;

	return( sum );
}

Vector4f Vector4f::operator- ( const Vector4f& Vector ) const
{
	Vector4f diff;

	diff.x = x - Vector.x;
	diff.y = y - Vector.y;
	diff.z = z - Vector.z;
	diff.w = w - Vector.w;

	return( diff );
}

Vector4f Vector4f::operator* ( float fScalar ) const
{
	Vector4f prod;

	prod.x = x * fScalar;
	prod.y = y * fScalar;
	prod.z = z * fScalar;
	prod.w = w * fScalar;

	return( prod );
}

Vector4f Vector4f::operator* ( const Vector4f& Vector ) const
{
    Vector4f prod;

    prod.x = x * Vector.x;
    prod.y = y * Vector.y;
    prod.z = z * Vector.z;
    prod.w = w * Vector.w;

    return( prod );
}

Vector4f Vector4f::operator/ ( float fScalar ) const
{
	Vector4f quot;
	if ( fScalar != 0.0f )
	{
		float fInvScalar = 1.0f / fScalar;
		quot.x = x * fInvScalar;
		quot.y = y * fInvScalar;
		quot.z = z * fInvScalar;
		quot.w = w * fInvScalar;
	}
	else
	{
		quot = Vector4f::ZERO;
	}

	return( quot );
}

Vector4f Vector4f::operator/ ( const Vector4f& Vector ) const
{
    Vector4f quot;
    quot.x = Vector.x != 0.0f ? x / Vector.x : 0.0f;
    quot.y = Vector.y != 0.0f ? y / Vector.y : 0.0f;
    quot.z = Vector.z != 0.0f ? z / Vector.z : 0.0f;
    quot.w = Vector.w != 0.0f ? w / Vector.w : 0.0f;

    return( quot );
}

Vector4f Vector4f::operator- ( ) const
{
	Vector4f neg;

	neg.x = -x;
	neg.y = -y;
	neg.z = -z;
	neg.w = -w;

	return( neg );
}

Vector4f& Vector4f::operator+= ( const Vector4f& Vector )
{
	x += Vector.x;
	y += Vector.y;
	z += Vector.z;
	w += Vector.w;

	return( *this );
}

Vector4f& Vector4f::operator-= ( const Vector4f& Vector )
{
	x -= Vector.x;
	y -= Vector.y;
	z -= Vector.z;
	w -= Vector.w;

	return( *this );
}

Vector4f& Vector4f::operator*= ( float fScalar )
{
	x *= fScalar;
	y *= fScalar;
	z *= fScalar;
	w *= fScalar;

	return( *this );
}

Vector4f& Vector4f::operator*= ( const Vector4f& Vector )
{
    x *= Vector.x;
    y *= Vector.y;
    z *= Vector.z;
    w *= Vector.w;

    return( *this );
}

Vector4f& Vector4f::operator/= ( float fScalar )
{
	if ( fScalar != 0.0f )
	{
		float fInvScalar = 1.0f / fScalar;	
		x *= fInvScalar;
		y *= fInvScalar;
		z *= fInvScalar;
		w *= fInvScalar;
	}
	else
	{
		*this = Vector4f::ZERO;
	}

	return( *this );
}

Vector4f& Vector4f::operator/= ( const Vector4f& Vector )
{
    x = Vector.x != 0.0f ? x / Vector.x : 0.0f;
    y = Vector.y != 0.0f ? y / Vector.y : 0.0f;
    z = Vector.z != 0.0f ? z / Vector.z : 0.0f;
    w = Vector.w != 0.0f ? w / Vector.w : 0.0f;

    return( *this );
}

void Vector4f::Clamp()
{
	if ( x > 1.0f )
        x = 1.0f;
	if ( x < 0.0f )
        x = 0.0f;

	if ( y > 1.0f )
        y = 1.0f;
	if ( y < 0.0f )
        y = 0.0f;

	if ( z > 1.0f )
        z = 1.0f;
	if ( z < 0.0f )
        z = 0.0f;

	if ( w > 1.0f )
        w = 1.0f;
	if ( w < 0.0f )
        w = 0.0f;
}

unsigned int Vector4f::toARGB()
{
	unsigned int result = 0;

	Clamp();
    
    result += static_cast<unsigned int>(255 * z);
    result += static_cast<unsigned int>(255 * y) << 8;
    result += static_cast<unsigned int>(255 * x) << 16;
    result += static_cast<unsigned int>(255 * w) << 24;

	return( result );
}

unsigned int Vector4f::toRGBA( )
{
	unsigned int result = 0;

	Clamp();

    result += static_cast<unsigned int>(255 * w);
    result += static_cast<unsigned int>(255 * z) << 8;
    result += static_cast<unsigned int>(255 * y) << 16;
    result += static_cast<unsigned int>(255 * x) << 24;
	
	return( result );
}

void Vector4f::fromARGB( unsigned int color )
{
    x = static_cast<float>((color & 0x00ff0000) >> 16) / (255.0f);	// red channel
    y = static_cast<float>((color & 0x0000ff00) >> 8) / (255.0f);	// green channel
    z = static_cast<float>((color & 0x000000ff)) / (255.0f);		// blue channel
    w = static_cast<float>((color & 0xff000000) >> 24) / (255.0f);	// alpha channel
}

Vector3f Vector4f::xyz() const
{
	return( Vector3f( x, y, z ) );
}

Vector2f Vector4f::xy() const
{
	return( Vector2f( x, y ) );
}

// Scalar + / - (declared in header)
Vector4f Vector4f::operator+ ( float fScalar ) const
{
	return Vector4f(x + fScalar, y + fScalar, z + fScalar, w + fScalar);
}

Vector4f Vector4f::operator- ( float fScalar ) const
{
	return Vector4f(x - fScalar, y - fScalar, z - fScalar, w - fScalar);
}

Vector4f& Vector4f::operator+= ( float fScalar )
{
	x += fScalar; y += fScalar; z += fScalar; w += fScalar;
	return *this;
}

Vector4f& Vector4f::operator-= ( float fScalar )
{
	x -= fScalar; y -= fScalar; z -= fScalar; w -= fScalar;
	return *this;
}

	} // namespace Math
} // namespace Dark