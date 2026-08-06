#ifndef __MODEL_H__
#define __MODEL_H__

#include <vector>
#include "geometry.h"

class Model {
private:
	std::vector<vec3> verts_;
	std::vector<std::vector<int> > faces_;
public:
	Model(const char *filename);
	~Model();
	int nverts();
	int nfaces();
	vec3 vert(const int i);
	vec3 vert(const int iface, const int nthvert) const;
	std::vector<int> face(int idx);
};

#endif //__MODEL_H__
