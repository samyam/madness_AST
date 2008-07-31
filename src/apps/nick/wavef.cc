/***********************************************************************
 * Here are some useful hydrogenic wave functions represented as madness 
 * functors. The bound states come from the Gnu Scientific Library. The
 * scattering states are generated with the confluent hypergeometric 
 * function. 
 * 
 * Using: Gnu Scientific Library
 *        http://www.netlib.org/toms/707
 * By:    Nick Vence
 **********************************************************************/

#include <nick/wavef.h>
#include <gsl/gsl_sf_trig.h>
#include <gsl/gsl_sf_coulomb.h>
#include <gsl/gsl_sf_gamma.h>
#include <gsl/gsl_sf_legendre.h>

//MPI printing macros
double tt;
#define PRINTLINE(str) if(world.rank()==0) cout << str << endl;
#define START_TIMER world.gop.fence(); tt=wall_time()
#define END_TIMER(msg) tt=wall_time()-tt;  if (world.rank()==0) printf("timer: %24.24s    took%8.2f seconds\n", msg, tt)
#define PRINT_COMPLEX(msg,re,im) if(world.rank()==0) printf("%34.34s %9.6f + %9.6fI\n", msg, re, im)

/***********************************************************************
 * The Scattering Wave Function
 * See Landau and Lifshitz Quantum Mechanics Volume 3
 * Third Edition Formula (136.9)
 **********************************************************************/
ScatteringWF::ScatteringWF(double M, double Z, const vector3D& kVec) : 
    WaveFunction(M,Z), kVec(kVec) 
{
    double sum = 0.0;
    for(int i=0; i<NDIM; i++) { sum += kVec[i]*kVec[i]; }
    k = sqrt(sum);
    //z = r*cos(th)
    //cos(th) = z/r
    costhK = kVec[2]/k;
}

complexd ScatteringWF::operator()(const vector3D& rVec) const
{
    double sum = 0.0;
    for(int i=0; i<NDIM; i++) { sum += rVec[i]*rVec[i]; }
    double r = sqrt(sum);
    double kDOTr = 0.0;
    for(int i=0; i<NDIM; i++) { kDOTr += rVec[i]*kVec[i]; }
    return exp(PI/(2*k))
	 * gamma(1.0+I/k)
 	 * exp(I*kDOTr)
	 * f11(-1.0*I/k, 1.0, -1.0*I*(k*r + kDOTr) );
}


/******************************************
 * BoundWF
 ******************************************/
BoundWF::BoundWF(double M, double Z, int nn, int ll, int mm ) : WaveFunction(M,Z)
{
    if(nn < 1) {
	cerr << "Thou shalt not have negative n!" << endl;
	exit(1);
    }
    if(ll<0 || ll>=nn) {
	cerr << "l has broken the quantum commandments!" << endl;
	exit(1);
    }
    if(abs(mm) > ll) {
	cerr << "m out of bounds error!" << endl;
	exit(1);
    }
    n=nn;
    l=ll;
    m=mm;
}

complexd BoundWF::operator()(const vector3D& rVec) const
{
    double sum = 0.0;
    for(int i=0; i<NDIM; i++) { sum += rVec[i]*rVec[i]; }
    double r = sqrt(sum);
    //z = r*cos(th)
    //cos(th) = z/r
    double costh = rVec[2]/r;
    if(m==0) {
	return complexd(gsl_sf_hydrogenicR(n, l, 1.0*_Z, r) 
		      * gsl_sf_legendre_sphPlm(l, m, costh), 0.0);
    }
    else {
	gsl_sf_result rPhi;
	gsl_sf_result phi ; 
	gsl_sf_rect_to_polar(rVec[0], rVec[1], &rPhi, &phi);
	return gsl_sf_hydrogenicR(n, l, _Z, r)
	    * gsl_sf_legendre_sphPlm(l, m, costh)
	    * exp(complexd(0.0, m*phi.val));
    }
}

/*************************
 *WaveFuction Constructor
 *************************/
WaveFunction::WaveFunction(double M, double Z) 
{
    _M = abs(M);
    _Z = Z;
}


/*****************************************
 *Exp[ I*(k.r) ]
 *****************************************/
Expikr::Expikr( const vector3D& kVec) : kVec(kVec)
{
    double sum = 0.0;
    for(int i=0; i<NDIM; i++) { sum += kVec[i]*kVec[i]; }
    k = sqrt(sum);
    //z = r*cos(th)
    //cos(th) = z/r
    costhK = kVec[2]/k;
}

complexd Expikr::operator()(const vector3D& rVec) const
{
    double kDOTr = 0.0;
    for(int i=0; i<NDIM; i++) {
			kDOTr += kVec[i]*rVec[i];
    }
    return exp(I*kDOTr);
}




    
/*******************************************************
 * Here is where I splice together my two representations of the hypergeometric
 * function. See personal journal C page 31 for a derivation of the cut off
 *******************************************************/
complexd f11(complexd AA, complexd BB, complexd ZZ)
{
    double k = 1.0/abs(imag(AA));
    if(abs(imag(ZZ)) <= 11.0 + 1.0/k + 0.5/(k*k) ) return conHyp(AA,BB,ZZ);
    else return aForm(AA,BB,ZZ);
}

/*********************************************************
 *The function from Barnett's code
 *Currently I'm not using it, but it may be faster in some regions of 1F1
 *I'll have to check it out later
 *********************************************************/
complexd hypergf(complexd AA, complexd BB, complexd XX)
{
    double EPS = 5E-15;
    int LIMIT  = 20000;
    int KIND   = 1;      	//Using normal precision in complex artithmetic 
    double ERR = 1E-18;
    int NITS   = 0;
    double FPMAX = 1E200;
    double ACC8  = 2E-18;
    double ACC16 = 2E-200;
    return   hypergf_(&AA, &BB, &XX, &EPS, &LIMIT, &KIND,
		      &ERR, &NITS, &FPMAX, &ACC8, &ACC16);
}

/*********************************************************
 *Tom707 Confluent Hypergeometric function
 ********************************************************/
complexd conHyp(complexd AA, complexd BB, complexd XX)
{
    int IP = 0;
    int LNCHF = 0;
    return conhyp_(&AA, &BB, &XX, &LNCHF, &IP);
}


/****************************************************************
 * The asymptotic form of the hypergeometric function given by
 * Abramowitz and Stegun 13.5.1
 * **************************************************************/
complexd aForm(complexd AA, complexd BB, complexd ZZ)
{
    complexd coeffA = gamma(BB)* exp(-1.0*I*PI*AA) * pow(ZZ,-AA)/gamma(BB-AA);
    complexd coeffB = gamma(BB)* exp(ZZ) * pow(ZZ,AA-BB)/gamma(AA);
    complexd termA(0,0);
    complexd termB(0,0);
    int maxTerms = 10;
    for(int n=0; n<=maxTerms; n++)
    {
	termA += pochhammer(AA,n)*pochhammer(1.0+AA-BB,n)*pow(-1.0*ZZ,-1.0*n)
	    /(double)fact(n);
    }
    for(int n=0; n<=maxTerms; n++)
    {
	termB += pochhammer(BB-AA,n)*pochhammer(1.0-AA,n)*pow(ZZ,-1.0*n)
	    /(double)fact(n);
    }
    return coeffA*termA + coeffB*termB;
}

void test1F1(World&, complexd (*func1F1)(complexd,complexd,complexd), const char* fileChar)
{
    cout << "Testing 1F1:==================================================" << endl;
    double r[] = { 1.0, 10.0, 100.0, 1000.0, 10000.0 };
    double k[] = { 1.0, 0.1, 0.01, 0.001};
    double theta = 0.0;
    double thetaK = PI-0.1;
    int rMAX   = sizeof(r)/sizeof(r[0]);
    int kMAX   = sizeof(k)/sizeof(k[0]);
    complexd BB(1.0,0.0);
    for(int i=0; i<kMAX; i++)
    {
		complexd AA(0.0, -1.0/k[i]);
		cout << "***********" << endl;
		for(int j=0; j<rMAX; j++)
		{
			complexd ZZ(0.0, -1*k[i]*r[j]*(1 + cos(theta)*cos(thetaK) ));
			printf( "%s[ %5.0fI, 1, %9.3f]= %+.10e, %+.10eI\n", fileChar, 
				imag(AA), imag(ZZ), real((*func1F1)(AA, BB, ZZ)),
				imag((*func1F1)(AA, BB, ZZ))
			);
		}
    }
}

int fact(int n)
{
    int result = 1;
    if( n<0 )
    {
	cout << "Factorial out of bounds error" << endl;
	cerr << "Factorial out of bounds error" << endl;
	return 0;
    }
    for(int i=n; i>1; i--) { result *= i; }
    return result;
}

void testFact(World& world)
{
    if(world.rank() == 0) 
	cout << "Testing Factorial:============================================" << endl;
    if(world.rank() == 0) cout << fact(0) << endl;
    if(world.rank() == 0) cout << fact(1) << endl;
    if(world.rank() == 0) cout << fact(2) << endl;
    if(world.rank() == 0) cout << fact(3) << endl;
    if(world.rank() == 0) cout << fact(-1) << endl;
}

complexd gamma(double re, double im)
{
    gsl_sf_result lnr;
    gsl_sf_result arg;
    gsl_sf_lngamma_complex_e(re, im, &lnr, &arg);
    complexd ANS(exp(lnr.val)*cos(arg.val), exp(lnr.val)*sin(arg.val) );
    return ANS;
}

complexd gamma(complexd AA)
{
    gsl_sf_result lnr;
    gsl_sf_result arg;
    gsl_sf_lngamma_complex_e(real(AA), imag(AA), &lnr, &arg);
    complexd ANS(exp(lnr.val)*cos(arg.val), exp(lnr.val)*sin(arg.val) );
    return ANS;
}

void testGamma(World& world)
{
    if(world.rank() == 0) cout << "Testing Gamma:================================================" << endl;
    if(world.rank() == 0) cout << "gamma(3.0,0.0) = " << gamma(3.0,0.0) << endl;
    if(world.rank() == 0) cout << "gamma(0.0,3.0) = " << gamma(0.0,3.0) << endl;
    if(world.rank() == 0) cout << "gamma(3.0,1.0) = " << gamma(3.0,1.0) << endl;
    if(world.rank() == 0) cout << "gamma(1.0,3.0) = " << gamma(1.0,3.0) << endl;
}
	
complexd pochhammer(complexd AA, int n)
{
    complexd VAL(1.0,0.0);
    for(int i=0; i<n; i++)
    {
			complexd II(i,0.0);
			VAL *= AA + II;
    }
    return VAL;
}

void testPochhammer(World& world)
{
    complexd ZERO(0.0,0.0);
    complexd ONE(1.0,0.0);
    complexd _ONE(-1.0,0.0);
    complexd Iplus3(3,1);
    if(world.rank() == 0) cout << "Testing Pochhammer:===========================================" << endl;
    if(world.rank() == 0) cout << "Pochhammer[" << ZERO << ", " << 0 <<"] = " << pochhammer(ZERO,0) << endl;
    if(world.rank() == 0) cout << "Pochhammer[" << ZERO << ", " << 1 <<"] = " << pochhammer(ZERO,1) << endl;
    if(world.rank() == 0) cout << "Pochhammer[" << ONE <<  ", " << 0 <<"] = " << pochhammer(ONE,0) << endl;
    if(world.rank() == 0) cout << "Pochhammer[" << ONE <<  ", " << 1 <<"] = " << pochhammer(ONE,1) << endl;
    if(world.rank() == 0) cout << "Pochhammer[" << I <<    ", " << 2 <<"] = " << pochhammer(I,2) << endl;
    if(world.rank() == 0) cout << "Pochhammer[" << I <<    ", " << 2 <<"] = " << pochhammer(I,2) << endl;
    if(world.rank() == 0) cout << "Pochhammer[" << Iplus3 <<", "<< 2 <<"] = " << pochhammer(Iplus3,2) << endl;
    if(world.rank() == 0) cout << "Pochhammer[" << _ONE << ", " << 3 <<"] = " << pochhammer(ZERO,3) << endl;
}

//The Coulomb potential
complexd V(const vector3D& r)
{
	return -1.0/sqrt(r[0]*r[0] + r[1]*r[1] + r[2]*r[2] + 1e-8);
}

void f11Tester(World& world) 
{
    if(world.rank() == 0)
	{
		cout.precision(3);
		testPochhammer(world);
		testFact(world);
		cout.precision(12);
		test1F1(world, f11   ,"   f11");
		if(world.rank() == 0) cout << 
		 "Testing the wave functions:===================================" << endl;
	}

//  Bound States
    START_TIMER;
    Function<complexd,NDIM> psi_100 = FunctionFactory<complexd,NDIM>(world).functor( 
	functorT(new BoundWF(1.0, 1.0, 1, 0, 0)));  
    END_TIMER("Projecting |100>            ");
    // A second way to declare a functor
    START_TIMER;
    functorT psiA(new BoundWF(1.0, 1.0, 2, 1, 1));  
    Function<complexd,NDIM> psi_211 = FunctionFactory<complexd,NDIM>(world).functor( psiA );
    END_TIMER("Projecting |211>    ");  
    
//  Checking orthonormality
    START_TIMER;
    complexd printMe = psi_100.inner(psi_100);
    PRINT_COMPLEX("<100|100> =                        ",real(printMe),imag(printMe));
    END_TIMER("<100|100>                            ");  
    printMe = psi_100.inner(psi_211);
    PRINT_COMPLEX("<100|211> =                        ",real(printMe),imag(printMe));
    END_TIMER("<100|211>                            "); 

//  z-axis k vector
    const double zHat[NDIM] = {0.0, 0.0, 1.0};
    vector3D k1Vec(zHat);

//	Calculate energy of |100>
    START_TIMER;
    Function<complexd,NDIM> dPsi_100 = diff(psi_100,0);
    END_TIMER("Differentiating Psi_100             ");
    START_TIMER;
    Function<complexd,NDIM> v = FunctionFactory<complexd,NDIM>(world).f(V);
    END_TIMER("Projecting 1/r                      ");
    START_TIMER;
    complexd KE = 3*0.5*(dPsi_100.inner(dPsi_100));
    END_TIMER("<dPsi_100|dPsi_100>                 ");
	PRINT_COMPLEX("KE =                              ",real(KE), imag(KE));
    START_TIMER;
    complexd PE = psi_100.inner(v*psi_100);
    END_TIMER("<Psi_100|V|Psi_100>                 ");
	PRINT_COMPLEX("PE =                              ",real(PE), imag(PE));
	PRINT_COMPLEX("The total energy is:              ",real(PE+KE), imag(PE+KE));

//	Calculate energy of |211>
	START_TIMER;
    Function<complexd,NDIM> dPsi_211 = diff(psi_211,0);
    END_TIMER("Projecting dPsi_211                ");
    START_TIMER;
    KE = 3*0.5*(dPsi_211.inner(dPsi_211));
    END_TIMER("<dPsi_211|dPsi_211>                 ");
	PRINT_COMPLEX("KE =                              ",real(KE), imag(KE));
    START_TIMER;
    PE = psi_211.inner(v*psi_211);
    END_TIMER("<Psi_211|V|Psi_211>                 ");
	PRINT_COMPLEX("PE =                              ",real(PE), imag(PE));
	PRINT_COMPLEX("The total energy is:              ",real(PE+KE), imag(PE+KE));

//  Scattering States
    START_TIMER;
    Function<complexd,NDIM> psi_k1 = FunctionFactory<complexd,NDIM>(world).functor( 
			functorT( new ScatteringWF(1.0, 1.0, k1Vec)));
	END_TIMER("Projecting |psi_k1>                 ");

//  Checking orthogonality
	START_TIMER;
	printMe = psi_100.inner(psi_k1);
    END_TIMER("Projecting dPsi_211                 ");
	PRINT_COMPLEX("<Psi_k1|100>                      ",real(printMe), imag(printMe));

    //  Linearly polarized laser operator 
    vector3D FVec(zHat);
    START_TIMER;
    Function<complexd,NDIM> laserOp = FunctionFactory<complexd,NDIM>(world).functor( 
                                      functorT( new Expikr(FVec) ));
	END_TIMER("Projecting ExpIkr                    ");
	START_TIMER;
    Function<complexd,NDIM> ket = laserOp*psi_100;
	END_TIMER("ExpIkr * Psi_100       ");
	START_TIMER;
	printMe =  psi_k1.inner(ket);
    END_TIMER("<psi_k1|Exp[ik.r]|100> =             ");
    PRINT_COMPLEX("<psi_k1|Exp[ik.r]|100> =           ",real(printMe), imag(printMe));

//  Off-axis k vector
    double k   = 1.0; 
    double thK = PI/4;
    const double temp2[NDIM] = {0.0, k*sin(thK), k*cos(thK)};
    vector3D k1_45Vec(temp2);

//  Off-Axis Positive Energy State
	START_TIMER;
    Function<complexd,NDIM> psi_k1_45 = FunctionFactory<complexd,NDIM>(world).functor( 
			functorT( new ScatteringWF(1.0, 1.0, k1_45Vec) ));
	END_TIMER("Projecting |psi_k1_45>               ");
	START_TIMER;
	printMe =  psi_k1_45.inner(laserOp*psi_100);
	END_TIMER("<psi_k1_45|Exp[ik.r]|100> =             ");
	PRINT_COMPLEX("<psi_k1_45|Exp[ik.r]|100> =           ",real(printMe),imag(printMe)); 
}