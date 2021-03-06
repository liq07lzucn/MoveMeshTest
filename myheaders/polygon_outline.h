#ifndef POLYGON_OUTLINE_H
#define POLYGON_OUTLINE_H

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/geometries/polygon.hpp>
#include <boost/geometry/algorithms/assign.hpp>
#include <boost/geometry/algorithms/buffer.hpp>
#include <boost/foreach.hpp>

#include <deal.II/base/point.h>

typedef boost::geometry::model::d2::point_xy<double> b_point;
typedef boost::geometry::model::polygon<b_point> b_polygon;


template <typename Point>
void list_coordinates(Point const& p, double &x )
{
    using boost::geometry::get;
    std::cout << "x = " << get<0>(p) << " y = " << get<1>(p) << std::endl;
    x= get<0>(p);
}


template <typename Point>
class access_coords{
public:
    access_coords(std::vector<double>& x_in, std::vector<double>& y_in)
        :
        x(x_in),
        y(y_in)
    {}

    inline void operator()(Point& p){
        using boost::geometry::get;
        std::cout << "x = " << get<0>(p) << " y = " << get<1>(p) << std::endl;
        x.push_back(get<0>(p));
        y.push_back(get<1>(p));
        std::cout << x.size() << std::endl;
    }

    std::vector<double> x;
    std::vector<double> y;
};

template<int dim>
class Polygon_Outline{
public:
    Polygon_Outline();
    std::vector<b_polygon> Polygons;
    boost::geometry::model::multi_polygon<b_polygon> mPoly;

    void addPolygon(std::vector<dealii::Point<dim>> newPoly);

    bool isPointInside(dealii::Point<dim> p );

    void PrintPolygons(unsigned int rank);

    void condense();

    void PrintdealPoly(std::vector<dealii::Point<dim> > newPoly);

    void serialize();
    void deserialize();

    void test_buffer(std::vector<dealii::Point<dim>> p);

    //void list_coordinates(b_point const& p);

    std::vector<int> serialized_ints;
    std::vector<double> serialized_dbls;

};

template <int dim>
Polygon_Outline<dim>::Polygon_Outline(){
    if (dim != 2){
        std::cerr << "The Polygon_Outline class cannot be used in other than 2D" << std::endl;
    }
    Polygons.clear();
    serialized_ints.clear();
    serialized_dbls.clear();
}

template <int dim>
void Polygon_Outline<dim>::addPolygon(std::vector<dealii::Point<dim> > newPoly){
    if (dim != 2){
        std::cerr << "The Polygon_Outline class cannot be used in other than 2D" << std::endl;
        std::cerr << "No polygon is added" << std::endl;
        return;
    }
    //if (rank == 1)
    //    PrintdealPoly(newPoly);
    b_polygon b_newpoly;
    std::vector<b_point> pnts;

    for (unsigned int i = 0; i < newPoly.size(); ++i){
        pnts.push_back(b_point(newPoly[i][0],newPoly[i][1]));
    }
    boost::geometry::assign_points(b_newpoly, pnts);
    boost::geometry::correct(b_newpoly);

    //if (rank == 1)
    //    boost::geometry::dsv(b_newpoly);

    if (Polygons.size() == 0){
        Polygons.push_back(b_newpoly);
        mPoly.push_back(b_newpoly);
    }else{
        std::vector<b_polygon> unionRes;
        bool added = false;
        for (unsigned int i = 0; i < Polygons.size(); ++i){
            if(!boost::geometry::intersects(Polygons[i]) && !boost::geometry::intersects(b_newpoly)){
                boost::geometry::union_(Polygons[i], b_newpoly, unionRes);
                if (unionRes.size() == 1){
                    b_polygon simplified;
                    boost::geometry::simplify(unionRes[0], simplified, 0.01);
                    boost::geometry::correct(simplified);
                    if (!boost::geometry::intersects(simplified)){
                        Polygons.at(i) = simplified;
                        added = true;
                        break;
                    }
                }

            }

//            try{
//                boost::geometry::union_(Polygons[i], b_newpoly, unionRes);
//                success = true;
//            }
//            catch(...){
//                std::cout << "Poly: " << boost::geometry::dsv(Polygons[i]) << std::endl;
//                std::cout << "newPoly: " << boost::geometry::dsv(b_newpoly) << std::endl;
//            }
            //if (rank == 1)
            //    std::cout << "union: " << unionRes.size() << std::endl;
        }

        if (!added){
            Polygons.push_back(b_newpoly);
        }

        condense();
    }
}

template <int dim>
bool Polygon_Outline<dim>::isPointInside(dealii::Point<dim> p){
    bool in = false;
    for (unsigned int i = 0; i < Polygons.size(); ++i){
        in = boost::geometry::covered_by(b_point(p[0],p[1]),Polygons[i]);
        if (in)
            break;
    }
    //std::cout << in << std::endl;
    return in;
}

template <int dim>
void Polygon_Outline<dim>::PrintPolygons(unsigned int rank){
    std::cout << "Rank " << rank << " has " << Polygons.size() << " polygons" << std::endl;
    for (unsigned int i = 0; i < Polygons.size(); ++i){
        std::cout << "Rank " << rank << " : " << boost::geometry::dsv(Polygons[i]) << std::endl;
    }
}

template <int dim>
void Polygon_Outline<dim>::PrintdealPoly(std::vector<dealii::Point<dim> > poly){
    std::cout << "[ ";
    for (unsigned int i = 0; i < poly.size(); ++i){
        std::cout << "(" << poly[i][0] << "," << poly[i][1] << ") ";
    }
    std::cout << std::endl;
}

template<int dim>
void Polygon_Outline<dim>::condense(){
    std::vector<int> dlt;
    if (Polygons.size() < 2)
        return;
    for (unsigned int i = 0; i < Polygons.size(); ++i){
        for (unsigned int j = i+1; j < Polygons.size(); ++j){
            std::vector<b_polygon> unionRes;
            if (!boost::geometry::intersects(Polygons[i]) && !boost::geometry::intersects(Polygons[j])){
                boost::geometry::union_(Polygons[i], Polygons[j], unionRes);
                if (unionRes.size() == 1){
                    b_polygon simplified;
                    boost::geometry::simplify(unionRes[0], simplified, 0.01);
                    Polygons.at(i) = simplified;
                    dlt.push_back(j);
                }
            }
        }
    }

    std::vector<b_polygon> temp;
    temp = Polygons;
    //std::cout << temp.size() << std::endl;
    Polygons.clear();
    for (unsigned int i = 0; i < temp.size(); ++i){
        bool dlt_this = false;
        for (int j = 0; j < dlt.size(); ++j){
            if (i == dlt[j]){
                dlt_this = true;
                break;
            }
        }
        if (!dlt_this){
            Polygons.push_back(temp[i]);
        }
    }
}

template <int dim>
void Polygon_Outline<dim>::serialize(){
    serialized_ints.clear();
    serialized_dbls.clear();
    // First int is the number of polygons
    serialized_ints.push_back(Polygons.size());
    for (unsigned int i = 0; i < Polygons.size(); ++i){
        // For each polygon send the size
        int Nv = 0;

//        std::vector<double> x, y;
//        access_coords<b_point> AC(x,y);
//        boost::geometry::for_each_point(Polygons[i], access_coords<b_point>( x, y));
//        //boost::geometry::for_each_point(Polygons[i], AC());
//        std::cout << serialized_dbls.size() << std::endl;
//        std::cout << y.size() << std::endl;

        for (auto it = boost::begin(boost::geometry::exterior_ring(Polygons[i]));
                  it != boost::end(boost::geometry::exterior_ring(Polygons[i])); ++it){
            serialized_dbls.push_back(boost::geometry::get<0>(*it));
            serialized_dbls.push_back(boost::geometry::get<1>(*it));
            Nv++;
            //std::cout << boost::geometry::get<0>(*it) << std::endl;
        }
        serialized_ints.push_back(Nv);
    }
}

template <int dim>
void Polygon_Outline<dim>::deserialize(){
    Polygons.clear();
    int i_cnt = 0;
    int d_cnt = 0;
    if (serialized_ints.size()>0){
        unsigned int Npoly = serialized_ints.at(i_cnt); i_cnt++;
        std::cout << "Npoly: " << Npoly << std::endl;
        for (unsigned int i = 0; i < Npoly; ++i){
            std::vector<dealii::Point<dim> > poly;
            unsigned int nv = serialized_ints.at(i_cnt); i_cnt++;
            for (unsigned k = 0; k < nv; ++k){
                dealii::Point<dim> pp;
                pp[0] = serialized_dbls.at(d_cnt);d_cnt++;
                pp[1] = serialized_dbls.at(d_cnt);d_cnt++;
                poly.push_back(pp);
            }
            addPolygon(poly);
        }
    }
}

//template <int dim>
//void Polygon_Outline<dim>::list_coordinates(b_point const& p){
//    std::cout << "x = " << boost::geometry::get<0>(p) << " y = " << boost::geometry::get<1>(p) << std::endl;
//}

template <int dim>
void Polygon_Outline<dim>::test_buffer(std::vector<dealii::Point<dim>> p){
    const double buffer_distance = 500;
    const int points_per_circle = 36;
    boost::geometry::strategy::buffer::distance_symmetric<double> distance_strategy(buffer_distance);
    boost::geometry::strategy::buffer::point_circle circle_strategy(points_per_circle);
    boost::geometry::strategy::buffer::side_straight side_strategy;
    boost::geometry::strategy::buffer::end_flat end_strategy;
    boost::geometry::strategy::buffer::join_round join_strategy(points_per_circle);

    boost::geometry::model::multi_polygon<b_polygon> result, simplified;

    boost::geometry::model::multi_point<b_point> mp;
    for (unsigned int i = 0; i < p.size(); ++i){
        boost::geometry::append(mp, b_point(p[i][0],p[i][1]));
    }

    boost::geometry::buffer(mp, result,
                    distance_strategy, side_strategy,
                    join_strategy, end_strategy, circle_strategy);

    boost::geometry::simplify(result, simplified, 100);



    BOOST_FOREACH(b_polygon const& p, simplified){
        for (auto it = boost::begin(boost::geometry::exterior_ring(p));
             it != boost::end(boost::geometry::exterior_ring(p)); ++it){
            std::cout << boost::geometry::get<0>(*it) << ", " << boost::geometry::get<1>(*it)<< std::endl;
        }
            //std::cout << "Buffer: " << boost::geometry::dsv(p) << std::endl;
    }
//    for (auto it = boost::begin(boost::geometry::exterior_ring(simplified));
//         it != boost::end(boost::geometry::exterior_ring(simplified)); ++it){
//        std::cout << boost::geometry::get<0>(*it) << ", " << boost::geometry::get<1>(*it)<< std::endl;
//    }
}


#endif // POLYGON_OUTLINE_H
