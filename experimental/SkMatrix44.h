#ifndef SkMatrix44_DEFINED
#define SkMatrix44_DEFINED

#include "SkScalar.h"

typedef float SkMScalar;
static const SkMScalar SK_MScalar1 = 1;
static inline SkMScalar SkDoubleToMScalar(double x) {
    return static_cast<float>(x);
}

struct SkVector4 {
	SkScalar fData[4];
};

class SkMatrix44 {
public:
	SkMatrix44();
	SkMatrix44(const SkMatrix44&);
	SkMatrix44(const SkMatrix44& a, const SkMatrix44& b);

	void setIdentity();

	void setTranslate(SkMScalar dx, SkMScalar dy, SkMScalar dz);
	void preTranslate(SkMScalar dx, SkMScalar dy, SkMScalar dz);
	void postTranslate(SkMScalar dx, SkMScalar dy, SkMScalar dz);

    void setScale(SkMScalar sx, SkMScalar sx, SkMScalar sx);
    void preScale(SkMScalar sx, SkMScalar sx, SkMScalar sx);
    void postScale(SkMScalar sx, SkMScalar sx, SkMScalar sx);

    void setScale(SkMScalar scale) {
        this->setScale(scale, scale, scale);
    }
    void preScale(SkMScalar scale) {
        this->preScale(scale, scale, scale);
    }
    void postScale(SkMScalar scale) {
        this->postScale(scale, scale, scale);
    }
    
	void setConcat(const SkMatrix44& a, const SkMatrix44& b);
	void preConcat(const SkMatrix44& m) {
        this->setConcat(*this, m);
    }
	void postConcat(const SkMatrix44& m) {
        this->setConcat(m, *this);
    }
	friend SkMatrix44 operator*(const SkMatrix44& a, const SkMatrix44& b) {
		return SkMatrix44(a, b);
	}

    /** If this is invertible, return that in inverse and return true. If it is
        not invertible, return false and ignore the inverse parameter.
     */
    bool invert(SkMatrix44* inverse) const;

    /** Apply the matrix to the src vector, returning the new vector in dst.
        It is legal for src and dst to point to the same memory.
     */
	void map(const SkScalar src[4], SkScalar dst[4]) const;
    void map(SkScalar vec[4]) const {
        this->map(vec, vec);
        }

	friend SkVector4 operator*(const SkMatrix44& m, const SkVector4& src) {
        SkVector4 dst;
        m.map(src.fData, dst.fData);
        return dst;
	}

    void dump() const;

private:
    /*  Stored in the same order as opengl:
         [3][0] = tx
         [3][1] = ty
         [3][2] = tz
     */
	SkMScalar fMat[4][4];

    double determinant() const;
};

#endif
