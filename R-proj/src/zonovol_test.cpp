

// VolEsti (volume computation and sampling library)

// Copyright (c) 20012-2018 Vissarion Fisikopoulos
// Copyright (c) 2018 Apostolos Chalkis

//Contributed and/or modified by Apostolos Chalkis, as part of Google Summer of Code 2018 program.
#define VOLESTI_DEBUG
#include <Rcpp.h>
#include <RcppEigen.h>
// [[Rcpp::depends(BH)]]
#define VOLESTI_DEBUG
#include <iterator>
//#include <fstream>
#include <vector>
#include <list>
//#include <algorithm>
#include <math.h>
#include <chrono>
#include "cartesian_geom/cartesian_kernel.h"
#include <boost/random.hpp>
#include <boost/random/uniform_int.hpp>
#include <boost/random/normal_distribution.hpp>
#include <boost/random/uniform_real_distribution.hpp>
#include "vars.h"
#include "polytopes.h"
//#include "ellipsoids.h"
#include "ballintersectconvex.h"
//#include "vpolyintersectvpoly.h"
#include "samplers.h"
#include "gaussian_samplers.h"
#include "gaussian_annealing.h"
#include "rounding.h"
#include "zonovol_heads/cg_hpoly.h"
//#include "gaussian_samplers.h"
//#include "gaussian_annealing.h"
#include "zonovol_heads/sampleTruncated.h"
#include "zonovol_heads/annealing_zono.h"
#include "zonovol_heads/outer_zono.h"
#include "zonovol_heads/cg_zonovol.h"
#include "zonovol_heads/ball_annealing.h"
#include "zonovol_heads/est_ratio1.h"



// [[Rcpp::plugins(cpp11)]]
// [[Rcpp::export]]
double vol_zono (Rcpp::Reference P, double e, Rcpp::Function rtmvnorm, Rcpp::Function mvrandn,
                 Rcpp::Function mvNcdf, bool verbose, bool relaxed=false, int Wst=0, double delta_in=0.0,
                 double var_in=0.0, double up_lim=0.2) {

    typedef double NT;
    typedef Cartesian <NT> Kernel;
    typedef typename Kernel::Point Point;
    typedef boost::mt19937 RNGType;
    typedef HPolytope <Point> Hpolytope;
    typedef VPolytope <Point, RNGType> Vpolytope;
    typedef Zonotope <Point> zonotope;
    typedef Eigen::Matrix<NT,Eigen::Dynamic,1> VT;
    typedef Eigen::Matrix<NT,Eigen::Dynamic,Eigen::Dynamic> MT;
    //unsigned int n_threads=1,i,j;

    bool rand_only = false,
            NN = false,
            birk = false;
            //verbose = false;
    unsigned int n_threads = 1;
    zonotope ZP;
    Rcpp::NumericMatrix A = P.field("G");

    unsigned int m=A.nrow();
    unsigned int n=A.ncol();
    //std::cout<<"hello"<<std::endl;
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    // the random engine with this seed
    RNGType rng(seed);
    boost::random::uniform_real_distribution<>(urdist);
    boost::random::uniform_real_distribution<> urdist1(-1,1);

    MT V = Rcpp::as<MT>(A);
    VT vec = VT::Ones(m);
    ZP.init(n, V, vec);
    MT G = V.transpose();
    MT ps = G.completeOrthogonalDecomposition().pseudoInverse();
    MT sigma = ps*ps.transpose();
    sigma = (sigma + sigma.transpose())/2.0;
    //std::cout<<sigma<<std::endl;
    for (int i1 = 0; i1 < m; ++i1) {
        sigma(i1,i1) = sigma(i1,i1) + 0.00000001;
    }
    //sigma = sigma + 0.00001*MT::DiagonalMatrix(m);

    VT l = VT::Zero(m) - VT::Ones(m);
    VT u = VT::Ones(m);

    //MT test = sampleTr(l, u, sigma, 10, mvrandn, G);
    std::list<Point> randPoints;
    NT delta, ratio;
    if (Wst==0) {
        Wst = 10*m;
    }
    double tstart1 = (double)clock()/(double)CLOCKS_PER_SEC;
    get_delta<Point>(ZP, l, u, sigma, rtmvnorm, mvrandn, mvNcdf, G, var_in, delta_in, up_lim, ratio, Wst, randPoints);
    double tstop1 = (double)clock()/(double)CLOCKS_PER_SEC;
    if(verbose) std::cout << "[1] computation of outer time = " << tstop1 - tstart1 << std::endl;
    delta = delta_in;

    std::pair<Point,NT> InnerB = ZP.ComputeInnerBall();
    NT C = 2.0, frac=0.1, ratio2 = 1.0-1.0/(NT(n));
    int W = 4*n*n+500, N = 500 * ((int) C) + ((int) (n * n / 2));

    vars<NT, RNGType> var2(1, n, 10 + n / 10, 1, 0.0, e, 0, 0.0, 0, InnerB.second, rng,
                           urdist, urdist1, -1.0, verbose, false, false, NN, birk, false,
                           false);
    vars_g<NT, RNGType> var1(n, 1, N, W, 1, e/10.0, InnerB.second, rng, C, frac,
                             ratio2, -1.0, false, verbose, false, false, NN, birk,
                             false, false);

    //sigma = variance * sigma;
    l = l - VT::Ones(m) * delta;
    u = u + VT::Ones(m) * delta;
    if (verbose) std::cout<<"delta = "<<delta<<" first estimation of ratio (t-test value) = "<<ratio<<std::endl;
    //std::cout<<l<<"\n"<<u<<std::endl;
    double tstart2 = (double)clock()/(double)CLOCKS_PER_SEC;
    NT vol = cg_volume_zono(ZP, var1, var2, InnerB, rtmvnorm, mvrandn, mvNcdf, sigma, l, u, Wst);
    double tstop2 = (double)clock()/(double)CLOCKS_PER_SEC;
    if(verbose) std::cout << "[2] outer volume estimation with cg algo time = " << tstop2 - tstart2 << std::endl;
    if (verbose) std::cout<<"volume of outer = "<<vol<<std::endl;


    MT AA; VT b;
    AA.resize(2 * m, m);
    b.resize(2 * m);
    for (unsigned int i = 0; i < m; ++i) {
        b(i) = 1.0 + delta;
        for (unsigned int j = 0; j < m; ++j) {
            if (i == j) {
                AA(i, j) = 1.0;
            } else {
                AA(i, j) = 0.0;
            }
        }
    }
    for (unsigned int i = 0; i < m; ++i) {
        b(i + m) = 1.0 + delta;
        for (unsigned int j = 0; j < m; ++j) {
            if (i == j) {
                AA(i + m, j) = -1.0;
            } else {
                AA(i + m, j) = 0.0;
            }
        }
    }
    MT T = ZP.get_T();
    MT Tt = T.transpose();
    MT A2 = AA*Tt;
    MT B = G*Tt;
    MT A3 = A2*B.completeOrthogonalDecomposition().pseudoInverse();
    Hpolytope HP;
    HP.init(n,A3,b);
    std::pair<Point, NT> InnerBall = HP.ComputeInnerBall();
    vars_g<NT, RNGType> var11(n, 1, N, W, 1, e/10.0, InnerBall.second, rng, C, frac,
                             ratio2, -1.0, false, false, false, false, NN, birk,
                             false, true);
    NT vol2 = volume_gaussian_annealing(HP, var11, var2, InnerBall);
    std::cout<<"\n\nvol of h-polytope = "<<vol2<<"\n\n"<<std::endl;


    NT countIn = ratio*1200.0, totCount = 1200.0;
    //std::cout<<"countIn = "<<countIn<<std::endl;

    MT sigma22 = var_in * sigma;
    //std::cout<<sigma<<std::endl;
    int count;
    double tstart3 = (double)clock()/(double)CLOCKS_PER_SEC;
    //MT sample = sampleTr(l, u, sigma, 8800, mvrandn, G, count);
    //countIn = countIn + NT(8800-count);
    //totCount = totCount + NT(8800-count);
    std::pair<MT,MT> samples;// = sample_cube(l, u, sigma, 8800, mvrandn, G);
    //MT sample = samples.first;
    MT sample;
    NT prob = test_botev<NT>(l, u, sigma22, 10000, mvNcdf);
    NT ratio22 = est_ratio_zono(ZP, prob, e/2.0, W, rtmvnorm, mvrandn, sigma22, G, l, u);


    if (verbose) std::cout<<"variance = "<<var_in<<std::endl;
    double tstop3 = (double)clock()/(double)CLOCKS_PER_SEC;
    if(verbose) std::cout << "[3] rejection time = " << tstop3 - tstart3 << std::endl;
    if (verbose) std::cout<<"final ratio = "<<ratio22<<std::endl;
    vol = vol * ratio22;

    countIn = 0.0;
    totCount = 0.0;

    randPoints.clear();
    Point q(n);
    MT Q0 = ZP.get_Q0().transpose();
    MT G2=ZP.get_mat().transpose();
    std::vector<NT> Zs;


    typedef Ball<Point> ball;
    typedef BallIntersectPolytope<zonotope , ball > ZonoBall;
    typedef std::list<Point> PointList;
    std::vector<ZonoBall> ZonoBallSet;
    std::vector<PointList> PointSets;
    std::vector<NT> ratios;
    NT p_value = 0.1;
    ZonoBall zb1, zb2;

    get_sequence_of_zonoballs<ball>(ZP, ZonoBallSet, PointSets, ratios,
                             p_value, var2, delta_in, Zs, relaxed);

    if (ZonoBallSet.size()==0) {
        if(ratios[0]==1) return vol;
        if(verbose) std::cout<<"no ball | ratio = "<<ratios[0]<<std::endl;
        vol = vol * est_ratio_zball_sym<Point>(ZP, sigma, G2, Q0, l, u, delta_in, ratios[0], e*0.8602325, var2);
    } else {
        if(verbose) std::cout<<"number of balls = "<<ZonoBallSet.size()<<std::endl;
        zb1 = ZonoBallSet[0];
        vol = vol * est_ratio_zonoballs(ZP, zb1, zb1, ratios[0], e, var2, true);

        for (int i = 0; i < ZonoBallSet.size()-1; ++i) {
            zb1 = ZonoBallSet[i];
            zb2 = ZonoBallSet[i+1];
            vol = vol * est_ratio_zonoballs(ZP, zb1, zb2, ratios[i], e, var2, false);
        }

        zb1 = ZonoBallSet[ZonoBallSet.size()-1];
        vol = vol * est_ratio_zball_sym<Point>(zb1, sigma, G2, Q0, l, u, delta_in, ratios[ratios.size()-1], e, var2);
    }

    //std::cout<<"final volume = "<<vol<<std::endl;
    return vol;
}
