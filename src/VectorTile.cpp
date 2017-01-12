#include "VectorTile.h"
#include <string>
#include "vector_tile20/vector_tile.pb.h"
#include <math.h>
#include <iostream>
#include <sstream>
#include <utility>
using namespace std;

string FeatureTypeToStr(int typeIn)
{
	::vector_tile::Tile_GeomType type = (::vector_tile::Tile_GeomType)typeIn;
	if(type == ::vector_tile::Tile_GeomType_UNKNOWN)
		return "Unknown";
	if(type == ::vector_tile::Tile_GeomType_POINT)
		return "Point";
	if(type == ::vector_tile::Tile_GeomType_LINESTRING)
		return "LineString";
	if(type == ::vector_tile::Tile_GeomType_POLYGON)
		return "Polygon";
	return "Unknown type";
}

string ValueToStr(const ::vector_tile::Tile_Value &value)
{
	if(value.has_string_value())
		return value.string_value();
	if(value.has_float_value())
	{
		stringstream ss;
		ss << value.float_value();
		return ss.str();
	}
	if(value.has_double_value())
	{
		stringstream ss;
		ss << value.double_value();
		return ss.str();
	}
	if(value.has_int_value())
	{
		stringstream ss;
		ss << value.int_value();
		return ss.str();
	}
	if(value.has_uint_value())
	{
		stringstream ss;
		ss << value.uint_value();
		return ss.str();
	}
	if(value.has_sint_value())
	{
		stringstream ss;
		ss << value.sint_value();
		return ss.str();
	}
	if(value.has_bool_value())
	{
		stringstream ss;
		ss << value.bool_value();
		return ss.str();
	}
	return "Error: Unknown value type";
}

inline double CheckWinding(LineLoop2D pts) 
{
	double total = 0.0;
	for(size_t i=0; i < pts.size(); i++)
	{
		size_t i2 = (i+1)%pts.size();
		double val = (pts[i2].first - pts[i].first)*(pts[i2].second + pts[i].second);
		total += val;
	}
	return total;
}

void DecodeGeometry(const ::vector_tile::Tile_Feature &feature,
	int extent, int tileZoom, int tileColumn, int tileRow, 
	vector<Point2D> &pointsOut, 
	vector<vector<Point2D> > &linesOut,
	vector<Polygon2D> &polygonsOut)
	
{
	long unsigned int numTiles = pow(2,tileZoom);
	double lonMin = tilex2long(tileColumn, tileZoom);
	double latMax = tiley2lat(numTiles-tileRow-1, tileZoom);//Why is latitude no where near expected?
	double lonMax = tilex2long(tileColumn+1, tileZoom);
	double latMin = tiley2lat(numTiles-tileRow, tileZoom);
	double dLat = latMax - latMin;
	double dLon = lonMax - lonMin;
	vector<Point2D> points;
	Polygon2D currentPolygon;
	bool currentPolygonSet = false;
	unsigned prevCmdId = 0;

	int cursorx = 0, cursory = 0;
	double prevx = 0.0, prevy = 0.0;// prevWinding = -1.0;
	pointsOut.clear();
	linesOut.clear();
	polygonsOut.clear();

	for(int i=0; i < feature.geometry_size(); i ++)
	{
		unsigned g = feature.geometry(i);
		unsigned cmdId = g & 0x7;
		unsigned cmdCount = g >> 3;
		if(cmdId == 1)//MoveTo
		{
			for(unsigned j=0; j < cmdCount; j++)
			{
				unsigned v = feature.geometry(i+1);
				int value1 = ((v >> 1) ^ (-(v & 1)));
				v = feature.geometry(i+2);
				int value2 = ((v >> 1) ^ (-(v & 1)));
				cursorx += value1;
				cursory += value2;
				double px = dLon * double(cursorx) / double(extent) + lonMin;
				double py = - dLat * double(cursory) / double(extent) + latMax;
				
				if (feature.type() == vector_tile::Tile_GeomType_POINT)
					pointsOut.push_back(Point2D(px, py));
				if (feature.type() == vector_tile::Tile_GeomType_LINESTRING && points.size() > 0)
				{
					linesOut.push_back(points);
					points.clear();
				}
				prevx = px; 
				prevy = py;
				i += 2;
				prevCmdId = cmdId;
			}
		}
		if(cmdId == 2)//LineTo
		{
			for(unsigned j=0; j < cmdCount; j++)
			{
				if(prevCmdId != 2)
					points.push_back(Point2D(prevx, prevy));
				unsigned v = feature.geometry(i+1);
				int value1 = ((v >> 1) ^ (-(v & 1)));
				v = feature.geometry(i+2);
				int value2 = ((v >> 1) ^ (-(v & 1)));
				cursorx += value1;
				cursory += value2;
				double px = dLon * double(cursorx) / double(extent) + lonMin;
				double py = - dLat * double(cursory) / double(extent) + latMax;

				points.push_back(Point2D(px, py));
				i += 2;
				prevCmdId = cmdId;
			}
		}
		if(cmdId == 7) //ClosePath
		{
			//Closed path does not move cursor in v1 to v2.1 of spec.
			// https://github.com/mapbox/vector-tile-spec/issues/49
			for(unsigned j=0; j < cmdCount; j++)
			{
				if (feature.type() == vector_tile::Tile_GeomType_POLYGON)
				{
					double winding = CheckWinding(points);
					if(winding >= 0.0)
					{
						if(currentPolygonSet)
						{
							//We are moving on to the next polygon, so store what we have collected so far
							polygonsOut.push_back(currentPolygon);
							currentPolygon.first.clear();
							currentPolygon.second.clear();
						}
						currentPolygon.first = points; //outer shape
						currentPolygonSet = true;
					}
					else
						currentPolygon.second.push_back(points); //inter shape
					
					//prevWinding = winding;
					points.clear();
					prevCmdId = cmdId;
				}				
			}
		}
	}

	if (feature.type() == vector_tile::Tile_GeomType_LINESTRING)
		linesOut.push_back(points);
	if (feature.type() == vector_tile::Tile_GeomType_POLYGON)
		polygonsOut.push_back(currentPolygon);
}

DecodeVectorTile::DecodeVectorTile(class DecodeVectorTileResults &output)
{
	this->output = &output;
}

DecodeVectorTile::~DecodeVectorTile()
{

}

void DecodeVectorTile::DecodeTileData(const std::string &tileData, int tileZoom, int tileColumn, int tileRow)
{
	vector_tile::Tile tile;
	bool parsed = tile.ParseFromString(tileData);
	if(!parsed)
		throw runtime_error("Failed to parse tile data");
	
	this->output->NumLayers(tile.layers_size());	

	for(int layerNum = 0; layerNum < tile.layers_size(); layerNum++)
	{
		const ::vector_tile::Tile_Layer &layer = tile.layers(layerNum);
		this->output->LayerStart(layer.name().c_str(), layer.version());

		//The spec says "Decoders SHOULD parse the version first to ensure that 
		//they are capable of decoding each layer." This has not been implemented.

		for(int featureNum = 0; featureNum < layer.features_size(); featureNum++)
		{
			const ::vector_tile::Tile_Feature &feature = layer.features(featureNum);

			map<string, string> tagMap;
			for(int tagNum = 0; tagNum < feature.tags_size(); tagNum+=2)
			{	
				const ::vector_tile::Tile_Value &value = layer.values(feature.tags(tagNum+1));
				tagMap[layer.keys(feature.tags(tagNum))] = ValueToStr(value);
			}

			vector<Point2D> points;
			vector<vector<Point2D> > lines;
			vector<Polygon2D> polygons;
			DecodeGeometry(feature, layer.extent(), tileZoom, tileColumn, tileRow, 
				points, lines, polygons);

			this->output->Feature(feature.type(), feature.has_id(), feature.id(), tagMap, 
				points, lines, polygons);
		}

		this->output->LayerEnd();
	}
}

// *********************************************

// https://wiki.openstreetmap.org/wiki/Slippy_map_tilenames#C.2FC.2B.2B
int long2tilex(double lon, int z) 
{ 
	return (int)(floor((lon + 180.0) / 360.0 * pow(2.0, z))); 
}

int lat2tiley(double lat, int z)
{ 
	return (int)(floor((1.0 - log( tan(lat * M_PI/180.0) + 1.0 / cos(lat * M_PI/180.0)) / M_PI) / 2.0 * pow(2.0, z))); 
}

double tilex2long(int x, int z) 
{
	return x / pow(2.0, z) * 360.0 - 180;
}

double tiley2lat(int y, int z) 
{
	double n = M_PI - 2.0 * M_PI * y / pow(2.0, z);
	return 180.0 / M_PI * atan(0.5 * (exp(n) - exp(-n)));
}

// ***********************************************

DecodeVectorTileResults::DecodeVectorTileResults()
{

}

DecodeVectorTileResults::~DecodeVectorTileResults()
{

}

void DecodeVectorTileResults::NumLayers(int numLayers)
{
	cout << "Num layers: " << numLayers << endl;
}

void DecodeVectorTileResults::LayerStart(const char *name, int version)
{
	cout << "layer name: " << name << endl;
	cout << "layer version: " << version << endl;
}

void DecodeVectorTileResults::LayerEnd()
{
	cout << "layer end" << endl;
}

void DecodeVectorTileResults::Feature(int typeEnum, bool hasId, 
	unsigned long long id, const map<string, string> &tagMap,
	vector<Point2D> &points, 
	vector<vector<Point2D> > &lines,
	vector<Polygon2D> &polygons)
{
	cout << typeEnum << "," << FeatureTypeToStr((::vector_tile::Tile_GeomType)typeEnum);
	if(hasId)
		cout << ",id=" << id;
	cout << endl;

	for(map<string, string>::const_iterator it = tagMap.begin(); it != tagMap.end(); it++)
	{
		cout << it->first << "=" << it->second << endl;
	}

	for(size_t i =0; i < points.size(); i++)
		cout << "POINT("<<points[i].first<<","<<points[i].second<<") ";
	for(size_t i =0; i < lines.size(); i++)
	{
		cout << "LINESTRING(";
		vector<Point2D> &linePts = lines[i];
		for(size_t j =0; j < linePts.size(); j++)
			cout << "("<<linePts[i].first<<","<<linePts[i].second<<") ";
		cout << ") ";
	}
	for(size_t i =0; i < polygons.size(); i++)
	{
		Polygon2D &polygon = polygons[i];
		cout << "POLYGON((";
		LineLoop2D &linePts = polygon.first; //Outer shape
		for(size_t j =0; j < linePts.size(); j++)
			cout << "("<<linePts[j].first<<","<<linePts[j].second<<") ";
		cout << ")";
		if(polygon.second.size() > 0)
		{
			//Inner shape
			cout << ",(";
			for(size_t k =0; k < polygon.second.size(); k++)
			{
				cout << "(";
				LineLoop2D &linePts2 = polygon.second[k];
				for(size_t j =0; j < linePts2.size(); j++)
					cout << "("<<linePts2[j].first<<","<<linePts2[j].second<<") ";
				cout << ") ";
			}
			cout << ")";
		}

		cout << ") ";
	}
	cout << endl;
}

