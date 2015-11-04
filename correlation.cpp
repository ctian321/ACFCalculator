#include <boost/program_options.hpp>
#include <boost/math/tools/roots.hpp>
#include <boost/math/tools/minima.hpp>

#include <boost/numeric/ublas/matrix.hpp>
#include <boost/numeric/ublas/lu.hpp>
#include <boost/numeric/ublas/vector.hpp>

#include <stdexcept>

#include <iostream>
#include <fstream>

//#include <sstream>
//#include <string>
//#include <cstdlib>
//#include <stdlib.h>
#include <vector>
//#include <iterator>
//#include <algorithm>
//#include <cmath>
//#include <numeric>

#define INTCUTOFF 0.05
#define DEFAULT_MAXCORR 1000
#define DEFAULT_TIMESTEP 1

/* compile: g++ correlation.cpp -o correlation */

/* syntax: ./correlation time-series.traj 

/* the time series is assumed to be in the format saved by the NAMD colvars module */

/* calculate diffusion coefficient of a harmonically-restrained degree of freedom by */
/* calculating the autocorrelation function a time series of a molecular dynamics */
/* simulation then integrating it */

/* Hummer, G. Position-dependent diffusion coefficients and free energies from Bayesian
  analysis of equilibrium and replica molecular dynamics simulations. New J. Phys. 2005,
  7, 34. */

/* internal units:
distance = angstrom
time = femptoseconds
velocity = Angstrom / femptoseconds
*/
//using namespace std;
namespace po = boost::program_options;

// http://vilipetek.com/2013/10/07/polynomial-fitting-in-c-using-boost/

std::vector<double> polyfit( const std::vector<double>& oX,
			const std::vector<double>& oY, int nDegree )
{
  using namespace boost::numeric::ublas;

  if ( oX.size() != oY.size() )
    throw std::invalid_argument( "X and Y vector sizes do not match" );

  // more intuative this way
  nDegree++;

  size_t nCount =  oX.size();
  matrix<double> oXMatrix( nCount, nDegree );
  matrix<double> oYMatrix( nCount, 1 );

  // copy y matrix
  for ( size_t i = 0; i < nCount; i++ )
    {
      oYMatrix(i, 0) = oY[i];
    }

  // create the X matrix
  for ( size_t nRow = 0; nRow < nCount; nRow++ )
    {
      double nVal = 1.0f;
      for ( int nCol = 0; nCol < nDegree; nCol++ )
	{
	  oXMatrix(nRow, nCol) = nVal;
	  nVal *= oX[nRow];
	}
    }

  // transpose X matrix
  matrix<double> oXtMatrix( trans(oXMatrix) );
  // multiply transposed X matrix with X matrix
  matrix<double> oXtXMatrix( prec_prod(oXtMatrix, oXMatrix) );
  // multiply transposed X matrix with Y matrix
  matrix<double> oXtYMatrix( prec_prod(oXtMatrix, oYMatrix) );

  // lu decomposition
  permutation_matrix<int> pert(oXtXMatrix.size1());
  const std::size_t singular = lu_factorize(oXtXMatrix, pert);
  // must be singular
  BOOST_ASSERT( singular == 0 );

  // backsubstitution
  lu_substitute(oXtXMatrix, pert, oXtYMatrix);

  // copy the result to coeff
  return std::vector<double>( oXtYMatrix.data().begin(), oXtYMatrix.data().end() );
}

double *vacf;
int nCorr;
double var;
double varVel;
double timestep;

double laplace(double *series, double timestep, double s, int length)
{
  double F=0.0;

  for(int i=0;i<length;++i)
    {
      F+=exp(-s*i*timestep)*series[i]*timestep;
    }
  return(F);
}

double denom(double s)
{
  double laplaceVACF=laplace(vacf, timestep, s, nCorr);
  double f=laplaceVACF*(s*var+varVel/s)-var*varVel;
  std::cout << s << " " << f << " " << var << " " << varVel << " " << nCorr << " " << laplaceVACF <<  std::endl;
  return(f);
}

/* Allen, M.; Tildesley, D. Computer Simulation of Liquids; Oxford Science Publications, Clarendon Press: Oxford, 1989. */

double *calcCorrelation(double *y, int nSamples, int nCorr)
{
  double *corr=new double[nCorr];
  int min;
  int t;
  int ttoMax;

  for(int i=0;i<nCorr;++i)
    {
      corr[i]=0.0;
    }

  for(int i=0;i<nSamples;++i)
    {
      ttoMax=nSamples;

      if(i+nCorr<nSamples)
	ttoMax=i+nCorr;

      for(int j=i;j<ttoMax;++j)
	{
	  t=j-i;
	  corr[t]+=y[i]*y[j];
	}
    }

  for(int i=0;i<nCorr;++i)
    {
      corr[i]=corr[i]/(nSamples-i);
    }

  return(corr);
}

double variance(double *y, int nSamples)
{
  double v2=0.0;
  for(int i=0;i<nSamples;++i)
    v2+=y[i]*y[i];
  v2/=nSamples;
  return(v2);
}

void subtract_average(double *y, int nSamples)
{
  double avg=0.0;
  
  for(int i=0;i<nSamples;++i)
    {
      avg+=y[i];
    }
  avg/=nSamples;

  for(int i=0;i<nSamples;++i)
    y[i]-=avg;
}

// calculate the velocity time series by numerically differentiating the position time series

double *velocity_series(double *y, int nSamples, double timestep)
{
  double *vel=new double[nSamples-2];
  double avg=0.0;
  std::cout<< "#velseries " << nSamples << std::endl;
  // calculate velocity by finite difference approach. units: A/fs
  for(int i=1;i<nSamples-1;++i)
    {    
      vel[i-1]=(y[i+1]-y[i-1])/(2.0*timestep);
    }

  for(int i=0;i<nSamples-2;++i)
    {
      avg+=vel[i];
    }
  avg/=(nSamples-2);

  for(int i=0;i<nSamples-2;++i)
    vel[i]-=avg;
  
  return(vel);
}

double integrateCorr(double *acf, int nCorr, double timestep)
{
  double I=0.0;
  double threshhold;

  threshhold=acf[0]*acf[0]*INTCUTOFF;

  for(int i=0;i<nCorr-1;++i)
    {
      //      if(acf[i]<threshhold)
      //	break;
      
      I+=0.5*(acf[i]+acf[i+1])*timestep;
    }
  return(I);
}

// read time series from filename fname
// file is formed like NAMD colvar traj file
// store the number of points in numSamples
// each line should store one time step

std::vector<double> readSeriesNAMD(char *fname, int &numSamples, int field)
{
  std::ifstream datafile(fname);
  std::vector<double> series;
  double *timeSeries;
  int i=0;
  std::string line;
  std::istringstream iss;
  int begin;
  
  if(field==1)
    begin=15;
  else if(field==2)
    begin=37;
  else
    begin=61;

  numSamples=0;

  while(getline(datafile,line))
    {
      if(line.at(0)!='#')
	{
	  try
	    {
	      std::string str2=line.substr(begin,23);
	      series.push_back(atof(str2.c_str()));
	      ++numSamples;
	    }
	  catch (std::exception& e)
	    {
	      break;
	    }
	}
    }

  return(series);
}

// read time series from filename fname
// store the number of points in numSamples
// each line should store one time step

std::vector<double> readSeriesRaw(char *fname, int &numSamples)
{
  std::ifstream datafile(fname);
  std::vector<double> series;
  double *timeSeries;
  int i=0;
  std::string line;
  std::istringstream iss;
  int begin;
  
  numSamples=0;

  while(getline(datafile,line))
    {
      if(line.at(0)!='#')
	{
	  try
	    {
	      series.push_back(atof(line.c_str()));
	      ++numSamples;
	    }
	  catch (std::exception& e)
	    {
	      break;
	    }
	}
    }

  return(series);
}

// calculates intercept by linear interpolation

double leastsquares(const std::vector<double>& x, const std::vector<double>& y)
{
    int n=x.size();
    double s_x  = accumulate(x.begin(), x.end(), 0.0);
    double s_y  = accumulate(y.begin(), y.end(), 0.0);
    double s_xx = inner_product(x.begin(), x.end(), x.begin(), 0.0);
    double s_xy = inner_product(x.begin(), x.end(), y.begin(), 0.0);
    double m=(n * s_xy - s_x * s_y) / (n * s_xx - s_x * s_x);
    double b=(s_y-m*s_x)/n;
    
    std::cout << "#m " << m << std::endl;
    std::cout << "#b " << b << std::endl;
    return(b);
}

//  fit y_i=f(x_i) to y=A.exp(-bx)
//  ln y = ln A - bx
// return A

double exp_fit_linearregression(const std::vector<double>& x, const std::vector<double>& y)
{
  std::vector<double> lny;
  int n=x.size();
  
  for(int i=0;i<x.size();++i)
    {
      lny.push_back(log(y[i]));
    }
  double intercept=leastsquares(x, lny);
  
  return(exp(intercept));
}

int main(int argc, char *argv[])
{
  bool write_acf;
  std::vector<double> series, seriesVel;
  double *velSeries;
  double *acf, *timeSeries;
  double I;
  char *fname, *acf_fname;
  double var_m2;
  int field=1;
  int numSamples;
  double varAnalytical;
  double k=10.0*4.184*1000;
  double varVelAnalytical;

  timestep=1.0;
  varAnalytical=8.314*298.15/k;
  varVelAnalytical=8.314*298.15/(18.01/1000.0)*1E-10;

  po::options_description desc("Allowed options");
  
  desc.add_options()
    ("help,h", "produce help message")
    ("timeseries,t", po::value<std::string>()->required(), "file name of time series")
    ("acf,a", po::value<std::string>()->required(), "file name to save autocorrelation functions in")
    ("timestep,s", po::value<double>(&timestep)->default_value(DEFAULT_TIMESTEP), "time between samples in time series file (fs)")
    ("maxcorr,m", po::value<int>(&nCorr)->default_value(DEFAULT_MAXCORR), "maximum number of time steps to calculate correlation functions over")
    ("field,f", po::value<int>(&field)->default_value(1), "index of field to read from time series file")
    ;

  try
    {
      po::variables_map vm;
      po::store(po::parse_command_line(argc, argv, desc), vm);
      po::notify(vm);

      if (vm.count("help"))
	{
	  std::cout << desc << "\n";
	  return 1;
	}
      
      if (vm.count("timeseries"))
	{
	  const std::string fname_str=vm["timeseries"].as<std::string>();
	  fname=(char *) fname_str.c_str();
	}
      else
	{
	  std::cout << "Time series file must be provided" << std::endl;
	  return(1);
	}
      
      if (vm.count("acf"))
	{
	  const std::string acf_fname_str=vm["timeseries"].as<std::string>();
	  acf_fname=(char *) acf_fname_str.c_str();
	  write_acf=true;
	}
      else
	{
	  std::cout << "#ACF will not be saved" << std::endl;
	  write_acf=false;
	}

      if (vm.count("timestep"))
	{
	  timestep=vm["timestep"].as<double>();
	  std::cout << "#Time step of " << timestep << " fs will be used." << std::endl;
	}
      else
	{
	  timestep=DEFAULT_TIMESTEP;
	  std::cout << "#Default timestep of " << std::endl;
	}
    }
  catch ( const std::exception& e )
    {
      std::cerr << e.what() << std::endl;
      return 1;
    }
  
  std::cout << "#varAnalytical " << varAnalytical << std::endl;
  std::cout << "#varVelAnalytical " << varVelAnalytical << std::endl;
  
  boost::math::tools::eps_tolerance<double> tol(10);

  // find singulatity by finding root in denominator
  //  std::pair<T, T> s_singularity =
  //  bracket_and_solve_root(cbrt_functor_1<T>(x), guess, factor, is_rising, tol, it);
  
  series=readSeriesNAMD(fname, numSamples, field);
  timeSeries=&series[0];

  subtract_average(timeSeries, numSamples);
  velSeries=velocity_series(timeSeries, numSamples, timestep);
  
  numSamples=numSamples-2;

  acf=calcCorrelation(timeSeries, numSamples, nCorr);
  vacf=calcCorrelation(velSeries, numSamples, nCorr);

  var=variance(timeSeries, numSamples);
  varVel=variance(velSeries, numSamples);

  if(write_acf)
    {
      std::ofstream acf_file(acf_fname);
      
      for(int i=0;i<nCorr;++i)
	acf_file << i << " " << acf[i] << " " << vacf[i] << std::endl;
  
      acf_file.close();
    }
  
  I=integrateCorr(acf, nCorr, timestep);
  
  //  double s=0.003;
  
  std::cout << "#" << var << std::endl;
  std::cout << "#varVel " << varVel << std::endl;
  std::cout << "#varVelAnalytical " << varVelAnalytical << std::endl;
  std::cout << "#nCorr " << nCorr << std::endl;
  
  // Step 1: Find value of s where denominator is a minimum(laplaceVACF*(s*var+varVel/s)-var*varVel)
  typedef std::pair<double, double> S_pair;
  double s_lower=0.000001;
  double s_upper=1.0;
  
  S_pair s_min_pair=boost::math::tools::brent_find_minima(denom, s_lower, s_upper, 30);
  double s_min=s_min_pair.first;

  std::cout << "#s_min = " << s_min << std::endl;
  return(1);
  
  // Step2: calculate s over range of [s_min, 5*s_min]
  
  double s=s_min;
  std::vector<double> s_values;
  std::vector<double> intDs;
  
  while(s<=5.0*s_min)
    {
      double laplaceVACF=laplace(vacf, timestep, s, nCorr);
      double Ds=-(laplaceVACF*var*varVel)/(laplaceVACF*(s*var+varVel/s)-var*varVel);

      std::cout << s << " " <<  Ds << " " << laplaceVACF <<  " " << -(laplaceVACF*var*varVel) << " " << 1.0/(laplaceVACF*(s*var+varVel/s)-var*varVel) << " " << (laplaceVACF*(s*var+varVel/s)-var*varVel) << std::endl;
      s_values.push_back(s);
      intDs.push_back(Ds);
      s=s+0.0001;
    }

  // Step 3
  exp_fit_linearregression(s_values, intDs);
  double limDs=leastsquares(s_values, intDs);

  std::cout << "#I = " << I << std::endl;
  std::cout << "#var = " << var << std::endl;
  std::cout << "#D = " << var*var/I << " A2/fs " << std::endl;
  std::cout << "#D = " << var*var/I*0.1 << " cm2/s " << std::endl;
  std::cout << "#Ds= " << limDs << " A2/s " << std::endl;

  delete[] acf;
  delete[] vacf;
  //  series.earse();
}
