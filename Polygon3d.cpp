
#include "Polygon3d.h"
#include "input.h"

Polygon3d::Polygon3d(Polygon2* p, std::string pid) {
  _id = pid;
  _p2 = p;
}

Polygon3d::~Polygon3d() {
  // TODO: clear memory properly
  std::cout << "I am dead" << std::endl;
}

Box Polygon3d::get_bbox2d() {
  return bg::return_envelope<Box>(*_p2);
}

std::string Polygon3d::get_id() {
  return _id;
}

Polygon2* Polygon3d::get_Polygon2() {
    return _p2;
}



//-------------------------------
//-------------------------------

Polygon3dBlock::Polygon3dBlock(Polygon2* p, std::string pid, std::string lifttype) : Polygon3d(p, pid) 
{
  _lifttype = lifttype;
  _is3d = false;
  _vertexelevations.assign(bg::num_points(*(_p2)), 9999);
}

std::string Polygon3dBlock::get_lift_type() {
  return _lifttype;
}

int Polygon3dBlock::get_number_vertices() {
  return int(2 * _vertices.size());
}

bool Polygon3dBlock::threeDfy() {
  build_CDT();
  _floorheight = this->get_floor_height();
  _roofheight = this->get_roof_height();
  _is3d = true;
  _zvaluesinside.clear();
  _zvaluesinside.shrink_to_fit();
  _vertexelevations.clear();
  _vertexelevations.shrink_to_fit();
  return true;
}

std::string Polygon3dBlock::get_3d_citygml() {
  std::stringstream ss;
  ss << "<cityObjectMember>";
  ss << "<bldg:Building>";
  ss << "<bldg:measuredHeight uom=\"#m\">";
  ss << this->get_roof_height();
  ss << "</bldg:measuredHeight>";
  ss << "<bldg:lod1Solid>";
  ss << "<gml:Solid>";
  ss << "<gml:exterior>";
  ss << "<gml:CompositeSurface>";
  //-- get floor
  ss << get_polygon_lifted_gml(this->_p2, this->get_floor_height(), false);
  //-- get roof
  ss << get_polygon_lifted_gml(this->_p2, this->get_roof_height(), true);
  //-- get the walls
  auto r = bg::exterior_ring(*(this->_p2));
  for (int i = 0; i < (r.size() - 1); i++) 
    ss << get_extruded_line_gml(&r[i], &r[i + 1], this->get_roof_height(), 0, false);
  ss << "</gml:CompositeSurface>";
  ss << "</gml:exterior>";
  ss << "</gml:Solid>";
  ss << "</bldg:lod1Solid>";
  ss << "</bldg:Building>";
  ss << "</cityObjectMember>";
  return ss.str(); 
}

std::string Polygon3dBlock::get_3d_csv() {
  std::stringstream ss;
  ss << this->get_id() << ";" << std::setprecision(2) << std::fixed << this->get_roof_height() << ";" << this->get_floor_height() << std::endl;
  return ss.str(); 
}

std::string Polygon3dBlock::get_obj_v() {
  std::stringstream ss;
  for (auto& v : _vertices)
    ss << std::setprecision(2) << std::fixed << "v " << bg::get<0>(v) << " " << bg::get<1>(v) << " " << this->get_roof_height() << std::endl;
  for (auto& v : _vertices)
    ss << std::setprecision(2) << std::fixed << "v " << bg::get<0>(v) << " " << bg::get<1>(v) << " " << this->get_floor_height() << std::endl;
  return ss.str();
}

std::string Polygon3dBlock::get_obj_f(int offset, bool floor) {
  std::stringstream ss;
  ss << "usemtl block" << std::endl;
  //-- roof
  for (auto& t : _triangles)
    ss << "f " << (t.v0 + 1 + offset) << " " << (t.v1 + 1 + offset) << " " << (t.v2 + 1 + offset) << std::endl;
  //-- side walls
  for (auto& s : _segments) {
    ss << "f " << (s.v1 + 1 + offset) << " " << (s.v0 + 1 + offset) << " " << (s.v0 + 1 + offset + _vertices.size()) << std::endl;  
    ss << "f " << (s.v0 + 1 + offset + _vertices.size()) << " " << (s.v1 + 1 + offset + _vertices.size()) << " " << (s.v1 + 1 + offset) << std::endl;  
  }
  //-- floor
  if (floor == true) {
    for (auto& t : _triangles)
      ss << "f " << (t.v0 + 1 + offset + _vertices.size()) << " " << (t.v1 + 1 + offset + _vertices.size()) << " " << (t.v2 + 1 + offset + _vertices.size()) << std::endl;  
  }
  return ss.str();
}

float Polygon3dBlock::get_roof_height() {
  if (_is3d == true)
    return _roofheight;
  if (_zvaluesinside.size() == 0)
    return -9999;
  std::string t = _lifttype.substr(_lifttype.find_first_of("-") + 1);
  if (t == "MAX") {
    double v = -9999;
    for (auto z : _zvaluesinside) {
      if (z > v)
        v = z;
    }
    return v;
  }
  else if (t == "MIN") {
    double v = 9999;
    for (auto z : _zvaluesinside) {
      if (z < v)
        v = z;
    }
    return v;
  }
  else if (t == "AVG") {
    double sum = 0.0;
    for (auto z : _zvaluesinside) 
      sum += z;
    return (sum / _zvaluesinside.size());
  }
  else if (t == "MEDIAN") {
    std::nth_element(_zvaluesinside.begin(), _zvaluesinside.begin() + (_zvaluesinside.size() / 2), _zvaluesinside.end());
    return _zvaluesinside[_zvaluesinside.size() / 2];
  }
  else {
    std::cout << "UNKNOWN HEIGHT" << std::endl;
  }
  return -9999;
}


float Polygon3dBlock::get_floor_height() {
  if (_is3d == true)
    return _floorheight;
  return *(std::min_element(std::begin(_vertexelevations), std::end(_vertexelevations)));
}

bool Polygon3dBlock::add_elevation_point(double x, double y, double z) {
  Point2 p(x, y);
  //-- 1. assign to polygon if inside
  if (bg::within(p, *(_p2)) == true)
    _zvaluesinside.push_back(z);
  //-- 2. add to the vertices of the pgn to find their heights
  assign_elevation_to_vertex(x, y, z);
  return true;
}


//-- TODO: okay here brute-force, use of flann is points>200 (to benchmark perhaps?)  
bool Polygon3dBlock::assign_elevation_to_vertex(double x, double y, double z) {
  //-- assign the MINIMUM to each
  Point2 p(x, y);
  Ring2 oring = bg::exterior_ring(*(_p2));
  for (int i = 0; i < oring.size(); i++) {
    if (bg::distance(p, oring[i]) <= 2.0)
      if (z < _vertexelevations[i])
        _vertexelevations[i] = z;
  }
  int offset = int(bg::num_points(oring));
  auto irings = bg::interior_rings(*(_p2));
  for (Ring2& iring: irings) {
    for (int i = 0; i < iring.size(); i++) {
      if (bg::distance(p, iring) <= 2.0)
        if (z < _vertexelevations[i + offset])
        _vertexelevations[i + offset] = z;
    }
    offset += bg::num_points(iring);
  }
  return true;
}


bool Polygon3dBlock::build_CDT() {
  std::stringstream ss;
  ss << bg::wkt(*(_p2));
  Polygon3 p3;
  bg::read_wkt(ss.str(), p3);
  getCDT(&p3, _vertices, _triangles, _segments);
  return true;
}


//-------------------------------
//-------------------------------

Polygon3dBoundary::Polygon3dBoundary(Polygon2* p, std::string pid) : Polygon3d(p, pid) 
{
}

std::string Polygon3dBoundary::get_lift_type() {
  return "BOUNDARY3D";
}

bool Polygon3dBoundary::threeDfy() {
  std::stringstream ss;
  ss << bg::wkt(*(_p2));
  Polygon3 p3;
  bg::read_wkt(ss.str(), p3);
  getCDT(&p3, _vertices, _triangles, _segments);
  return true;
}

int Polygon3dBoundary::get_number_vertices() {
  return int(_vertices.size());
}


std::string Polygon3dBoundary::get_3d_citygml() {
  return "EMPTY"; 
}

std::string Polygon3dBoundary::get_3d_csv() {
  return "EMPTY"; 
}

std::string Polygon3dBoundary::get_obj_v() {
  std::stringstream ss;
  for (auto& v : _vertices)
    ss << std::setprecision(2) << std::fixed << "v " << bg::get<0>(v) << " " << bg::get<1>(v) << " " << bg::get<2>(v) << std::endl;
  return ss.str();
}

std::string Polygon3dBoundary::get_obj_f(int offset, bool floor) {
  std::stringstream ss;
  ss << "usemtl boundary3d" << std::endl;
  for (auto& t : _triangles)
    ss << "f " << (t.v0 + 1 + offset) << " " << (t.v1 + 1 + offset) << " " << (t.v2 + 1 + offset) << std::endl;
  return ss.str();
}

bool Polygon3dBoundary::add_elevation_point(double x, double y, double z) {
  _lidarpts.push_back(Point3(x, y, z));
  return true;
}


//-------------------------------
//-------------------------------

Polygon3dTin::Polygon3dTin(Polygon2* p, std::string pid, std::string lifttype) : Polygon3d(p, pid) 
{
  _lifttype = lifttype;
  std::string t = lifttype.substr(lifttype.find_first_of("-") + 1);
  if (t == "ALL")
    _thin_factor = 0;
  else 
    _thin_factor = std::stoi(t);
  _vertexelevations.resize(bg::num_points(*(_p2)));
}

std::string Polygon3dTin::get_lift_type() {
  return _lifttype;
}

bool Polygon3dTin::threeDfy() {
  std::stringstream ss;
  ss << bg::wkt(*(_p2));
  Polygon3 p3;
  bg::read_wkt(ss.str(), p3);
  add_elevations_to_boundary(p3);
  getCDT(&p3, _vertices, _triangles, _segments, _lidarpts);
  return true;
}


void Polygon3dTin::add_elevations_to_boundary(Polygon3 &p3) {
  float avg = 0.0;
  int count = 0;
  for (int i = 0; i < _vertexelevations.size(); i++) {
    if (std::get<0>(_vertexelevations[i]) != 0) {
      float z = std::get<1>(_vertexelevations[i]) / (float(std::get<0>(_vertexelevations[i])));
      avg += z;
      count++;  
    }
  }
  avg = avg / count;
  Ring3 oring = bg::exterior_ring(p3);
  for (int i = 0; i < oring.size(); i++) {
    if (std::get<0>(_vertexelevations[i]) != 0) {
      float z = std::get<1>(_vertexelevations[i]) / (float(std::get<0>(_vertexelevations[i])));
      bg::set<2>(bg::exterior_ring(p3)[i], z);
    }
    else {
      bg::set<2>(bg::exterior_ring(p3)[i], avg);
    }
  }
  auto irings = bg::interior_rings(p3);
  std::size_t offset = bg::num_points(p3);
  for (Ring3& iring: irings) {
    for (int i = 0; i < iring.size(); i++) {
      if (std::get<0>(_vertexelevations[i]) != 0) {
        float z = std::get<1>(_vertexelevations[i]) / (float(std::get<0>(_vertexelevations[i])));
        bg::set<2>(bg::exterior_ring(p3)[i], z);
      }
      else {
        bg::set<2>(bg::exterior_ring(p3)[i], avg);
      }
    }
    offset += bg::num_points(iring);
  }
}

int Polygon3dTin::get_number_vertices() {
  return int(_vertices.size());
}


std::string Polygon3dTin::get_3d_citygml() {
  //-- TODO: CityGML implementation for TIN type
  return "EMTPY";
}

std::string Polygon3dTin::get_3d_csv() {
  return "EMPTY"; 
}

std::string Polygon3dTin::get_obj_v() {
  std::stringstream ss;
  for (auto& v : _vertices)
    ss << std::setprecision(2) << std::fixed << "v " << bg::get<0>(v) << " " << bg::get<1>(v) << " " << bg::get<2>(v) << std::endl;
  return ss.str();
}

std::string Polygon3dTin::get_obj_f(int offset, bool floor) {
  std::stringstream ss;
  ss << "usemtl tin" << std::endl;
  for (auto& t : _triangles)
    ss << "f " << (t.v0 + 1 + offset) << " " << (t.v1 + 1 + offset) << " " << (t.v2 + 1 + offset) << std::endl;
  return ss.str();
}


bool Polygon3dTin::add_elevation_point(double x, double y, double z) {
  Point2 p(x, y);
  //-- 1. add points to surface if inside
  if (bg::within(p, *(_p2)) == true) {
    if (_thin_factor == 0)
      _lidarpts.push_back(Point3(x, y, z));
    else {
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<int> dis(1, _thin_factor); 
      if (dis(gen) == 1)
        _lidarpts.push_back(Point3(x, y, z));
    }
  }
  //-- 2. add to the vertices of the pgn to find their heights
  assign_elevation_to_vertex(x, y, z);
  return true;
}


//-- TODO: brute-force == not good
bool Polygon3dTin::assign_elevation_to_vertex(double x, double y, double z) {
  //-- assign the AVERAGE to each
  Point2 p(x, y);
  Ring2 oring = bg::exterior_ring(*(_p2));
  for (int i = 0; i < oring.size(); i++) {
    if (bg::distance(p, oring[i]) <= 2.0) {
      std::get<0>(_vertexelevations[i]) += 1;
      std::get<1>(_vertexelevations[i]) += z;  
    }
  }
  int offset = int(bg::num_points(oring));
  auto irings = bg::interior_rings(*(_p2));
  for (Ring2& iring: irings) {
    for (int i = 0; i < iring.size(); i++) {
      if (bg::distance(p, iring) <= 2.0) {
        std::get<0>(_vertexelevations[i + offset]) += 1;
        std::get<1>(_vertexelevations[i + offset]) += z;
      }
    }
    offset += bg::num_points(iring);
  }
  return true;
}

