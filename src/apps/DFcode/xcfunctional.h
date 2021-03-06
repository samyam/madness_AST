#ifndef MOLDFT_XCMOLDFT_H
#define MOLDFT_XCMOLDFT_H

#include <madness_config.h>

/// \file moldft/xcfunctional.h
/// \brief Defines interface for DFT XC functionals
/// \ingroup moldft

#include <tensor/tensor.h>
#include <vector>
#include <algorithm>
#include <utility>
#include <mra/key.h>

#ifdef MADNESS_HAS_LIBXC
#include <xc.h>
#endif

/// Compute the spin-restricted LDA potential using unaryop (only for the initial guess)
struct xc_lda_potential {
    xc_lda_potential() {}

    void operator()(const madness::Key<3> & key, madness::Tensor<double>& t) const
    {
        int x_rks_s__(const double *r__, double *f, double * dfdra);
        int c_rks_vwn5__(const double *r__, double *f, double * dfdra);
        double* rho = t.ptr();
        for (int i=0; i<t.size(); i++) {
            double r = std::max(rho[i],1e-12);
            double q, dq1, dq2;
            x_rks_s__(&r, &q, &dq1);
            c_rks_vwn5__(&r, &q, &dq2);
            rho[i] = dq1 + dq2;
        }
    }
};

#ifdef MADNESS_HAS_MADXC
//#error
struct xc_func_type {
  int   number;   /* indentifier number */                                                                                                   
  bool nspin;      /* XC_UNPOLARIZED or XC_POLARIZED  */
  int  kind;     /* XC_EXCHANGE, XC_CORRELATION, or XC_EXCHANGE_CORRELATION */                                                              
  std::string name;     /* name of the functional, e.g. "PBE" */                                                                             
  int   family;   /* type of the functional, e.g. XC_FAMILY_GGA */                                                                           
  double   coef;   /* coefficient */
};  
#endif

/// Simplified interface to XC functionals
class XCfunctional {
protected:
    bool spin_polarized;        ///< True if the functional is spin polarized
    double hf_coeff;            ///< Factor multiplying HF exchange (+1.0 gives HF)
#if defined(MADNESS_HAS_MADXC) ||defined( MADNESS_HAS_LIBXC)
    std::vector< std::pair<xc_func_type*,double> > funcs;
    int nderiv;
#endif

#ifdef MADNESS_HAS_LIBXC
    void make_libxc_args(const std::vector< madness::Tensor<double> >& t,
                         madness::Tensor<double>& rho,
                         madness::Tensor<double>& sigma,
                         madness::Tensor<double>& delrho) const;
#endif

#ifdef MADNESS_HAS_MADXC
    void make_xc_args(const std::vector< madness::Tensor<double> >& t,
                         std::vector< madness::Tensor<double> >& rho,
                         std::vector< madness::Tensor<double> >& sigma,
                         std::vector< madness::Tensor<double> >& delrho) const;
#endif

    /// Smoothly switches between constant (x<xmin) and linear function (x>xmax)

    /// \f[
    /// f(x,x_{\mathrm{min}},x_{\mathrm{max}}) = \left\{
    ///   \begin{array}{ll}
    ///     x_{\mathrm{min}}                       & x < x_{\mathrm{min}}                   \\
    ///     p(x,x_{\mathrm{min}},x_{\mathrm{max}}) & x_{\mathrm{min}} \leq x_{\mathrm{max}} \\
    ///     x                                      & x_{\mathrm{max}} < x
    ///   \end{array}
    /// \right.
    /// \f]
    /// where \f$p(x)\f$ is the unique quintic polynomial that
    /// satisfies \f$p(x_{min})=x_{min}\f$, \f$p(x_{max})=x_{max}\f$,
    /// \f$dp(x_{max})/dx=1\f$, and
    /// \f$dp(x_{min})/dx=d^2p(x_{min})/dx^2=d^2p(x_{max})/dx^2=0\f$.
    static void polyn(const double x, double& p, double& dpdx) {
        // All of the static const stuff is evaluated at compile time

        static const double xmin = 1e-10; // <<<< MINIMUM VALUE OF DENSITY
        static const double xmax = 1e-8;  // <<<< DENSITY SMOOTHLY MODIFIED BELOW THIS VALUE

        static const double xmax2 = xmax*xmax;
        static const double xmax3 = xmax2*xmax;
        static const double xmin2 = xmin*xmin;
        static const double xmin3 = xmin2*xmin;
        static const double r = 1.0/((xmax-xmin)*(-xmin3+(3.0*xmin2+(-3.0*xmin+xmax)*xmax)*xmax));
        static const double a0 = xmax3*xmin*(xmax-4.0*xmin)*r;
        static const double a = xmin2*(xmin2+(-4.0*xmin+18.0*xmax)*xmax)*r;
        static const double b = -6.0*xmin*xmax*(3.0*xmax+2.0*xmin)*r;
        static const double c = (4.0*xmin2+(20.0*xmin+6.0*xmax)*xmax)*r;
        static const double d = -(8.0*xmax+7.0*xmin)*r;
        static const double e = 3.0*r;

        if (x > xmax) {
            p = x;
            dpdx = 1.0;
        }
        else if (x < xmin) {
            p = xmin;
            dpdx = 0.0;
        }
        else {
            p = a0+(a+(b+(c+(d+e*x)*x)*x)*x)*x;
            dpdx = a+(2.0*b+(3.0*c+(4.0*d+5.0*e*x)*x)*x)*x;
        }
    }

    static double munge(double rho) {
        double p, dpdx;
        polyn(rho, p, dpdx);
        return p;
    }

    static void munge2(double& rho, double& sigma) {
        // rho(x) --> p(rho(x))
        // d/dx p(rho(x)) --> dp/drho * drho/dx
        if (sigma < 0.0) sigma = 0.0;
        double p, dpdx;
        polyn(rho, p, dpdx);
        rho = p;
        sigma *= dpdx*dpdx;
    }
    static void munge_der(double& rhoa, double& sigma, double& drx, double& dry, double& drz) {
        // rho(x) --> p(rho(x))
        // d/dx p(rho(x)) --> dp/drho * drho/dx
        if (sigma < 0.0) sigma = 0.0;
        double p, dpdx;
        polyn(rhoa, p, dpdx);
        rhoa = p;
        sigma *= dpdx*dpdx;
        drx *=dpdx;
        dry *=dpdx;
        drz *=dpdx;
    }
    static void munge5(double& rhoa, double& rhob, double& saa, double& sab, double& sbb) {
        // rho(x) --> p(rho(x))
        // d/dx p(rho(x)) --> dp/drho * drho/dx
        if (saa < 0.0) saa = 0.0;
        if (sab < 0.0) sab = 0.0;
        if (sbb < 0.0) sbb = 0.0;
        double pa, pb, dpadx, dpbdx;
        polyn(rhoa, pa, dpadx);
        polyn(rhob, pb, dpbdx);
        rhoa = pa;
        rhob = pb;
        saa *= dpadx*dpadx;
        sab *= dpadx*dpbdx;
        sbb *= dpbdx*dpbdx;
    }
    static void munge5_der(double& rhoa, double& rhob, double& saa, double& sab, double& sbb,
                           double& drax, double& dray, double& draz,
                           double& drbx, double& drby, double& drbz) {
        // rho(x) --> p(rho(x))
        // d/dx p(rho(x)) --> dp/drho * drho/dx
        if (saa < 0.0) saa = 0.0;
        if (sab < 0.0) sab = 0.0;
        if (sbb < 0.0) sbb = 0.0;
        double pa, pb, dpadx, dpbdx;
        polyn(rhoa, pa, dpadx);
        polyn(rhob, pb, dpbdx);
        rhoa = pa;
        rhob = pb;
        saa *= dpadx*dpadx;
        sab *= dpadx*dpbdx;
        sbb *= dpbdx*dpbdx;

        drax *=dpadx;
        dray *=dpadx;
        draz *=dpadx;

        drbx *=dpbdx;
        drby *=dpbdx;
        drbz *=dpbdx;
    }

public:
    /// Default constructor is required
    XCfunctional();

    /// Initialize the object from the user input data

    /// @param[in] input_line User input line (without beginning XC keyword)
    /// @param[in] polarized Boolean flag indicating if the calculation is spin-polarized
    void initialize(const std::string& input_line, bool polarized);

    /// Destructor
    ~XCfunctional();

    /// Returns true if the potential is lda
    bool is_lda() const;

    /// Returns true if the potential is gga (needs first derivatives)
    bool is_gga() const;

    /// Returns true if the potential is meta gga (needs second derivatives ... not yet supported)
    bool is_meta() const;

    /// Returns true if there is a DFT functional (false probably means Hatree-Fock exchange only)
    bool is_dft() const;

    /// Returns true if the functional is spin_polarized
    bool is_spin_polarized() const
    {
        return spin_polarized;
    }

    /// Returns true if the second derivative of the functional is available (not yet supported)
    bool has_fxc() const;

    /// Returns true if the third derivative of the functional is available (not yet supported)
    bool has_kxc() const;

    /// Returns the value of the hf exact exchange coefficient
    double hf_exchange_coefficient() const
    {
        return hf_coeff;
    }

    /// Computes the energy functional at given points

    /// This uses the convention that the total energy is \f$ E[\rho] = \int \epsilon[\rho(x)] dx\f$
    ///
    /// Any HF exchange contribution must be separately computed.
    ///
    /// Items in the vector argument \c t are interpreted as follows
    ///  - Spin un-polarized
    ///    - \c t[0] = \f$ \rho_{\alpha} \f$
    ///    - \c t[1] = \f$ \sigma_{\alpha\alpha} = \nabla \rho_{\alpha}.\nabla \rho_{\alpha} \f$ (GGA only)
    ///  - Spin polarized
    ///    - \c t[0] = \f$ \rho_{\alpha} \f$
    ///    - \c t[1] = \f$ \rho_{\beta} \f$
    ///    - \c t[2] = \f$ \sigma_{\alpha\alpha} = \nabla \rho_{\alpha}.\nabla \rho_{\alpha} \f$ (GGA only)
    ///    - \c t[3] = \f$ \sigma_{\alpha\beta}  = \nabla \rho_{\alpha}.\nabla \rho_{\beta} \f$ (GGA only)
    ///    - \c t[4] = \f$ \sigma_{\beta\beta}   = \nabla \rho_{\beta}.\nabla \rho_{\beta} \f$ (GGA only)
    ///
    /// @param t The input densities and derivatives as required by the functional
    /// @return The exchange-correlation energy functional
    madness::Tensor<double> exc(const std::vector< madness::Tensor<double> >& t , const int ispin) const;

    /// Computes components of the potential (derivative of the energy functional) at np points

    /// Any HF exchange contribution must be separately computed.
    ///
    /// See the documenation of the \c exc() method for contents of the input \c t[] argument
    ///
    /// We define \f$ \sigma_{\mu \nu} = \nabla \rho_{\mu} . \nabla \rho_{\nu} \f$
    /// with \f$ \mu, \nu = \alpha\f$ or \f$ \beta \f$.
    ///
    /// For unpolarized GGA, matrix elements of the potential are
    /// \f$
    ///   < \phi | \hat V | \psi > = \int \left( \frac{\partial \epsilon}{\partial \rho_{\alpha}} \phi \psi
    ///                  +  \left( 2 \frac{\partial \epsilon}{\partial \sigma_{\alpha \alpha}} + \frac{\partial \epsilon}{\partial \sigma_{\alpha \beta}} \right) \nabla \rho_{\alpha} . \nabla \left( \phi \psi \right) \right) dx
    /// \f$
    ///
    /// For polarized GGA, matrix elements of the potential are
    /// \f$
    ///   < \phi_{\alpha} | \hat V | \psi_{\alpha} > = \int \left( \frac{\partial \epsilon}{\partial \rho_{\alpha}} \phi \psi
    ///                  +  \left( 2 \frac{\partial \epsilon}{\partial \sigma_{\alpha \alpha}} \nabla \rho_{\alpha}  + \frac{\partial \epsilon}{\partial \sigma_{\alpha \beta}} \nabla \rho_{\beta}  \right) . \nabla \left( \phi \psi \right) \right) dx
    /// \f$
    ///
    /// Integrating the above by parts and assuming free-space or periodic boundary conditions
    /// we obtain that the local multiplicative form of the GGA potential is
    /// \f$
    ///    V_{\alpha} =  \frac{\partial \epsilon}{\partial \rho_{\alpha}} - \left(\nabla . \left(2 \frac{\partial \epsilon}{\partial \sigma_{\alpha \alpha}} \nabla \rho_{\alpha}  + \frac{\partial \epsilon}{\partial \sigma_{\alpha \beta}} \nabla \rho_{\beta}  \right)  \right)
    /// \f$
    ///
    /// Until we get a madness::Function operation that can produce
    /// multiple results we need to compute components of the
    /// functional and potential separately:
    ///
    /// - Spin un-polarized
    ///   - \c what=0 \f$ \frac{\partial \epsilon}{\partial \rho_{\alpha}}\f$
    ///   - \c what=1 \f$ 2 \frac{\partial \epsilon}{\partial \sigma_{\alpha \alpha}} + \frac{\partial \epsilon}{\partial \sigma_{\alpha \beta}}\f$ (GGA only)
    /// - Spin polarized
    ///   - \c what=0 \f$ \frac{\partial \epsilon}{\partial \rho_{\alpha}}\f$
    ///   - \c what=1 \f$ \frac{\partial \epsilon}{\partial \rho_{\beta}}\f$
    ///   - \c what=2 \f$ \frac{\partial \epsilon}{\partial \sigma_{\alpha \alpha}} \f$
    ///   - \c what=3 \f$ \frac{\partial \epsilon}{\partial \sigma_{\alpha \beta}} \f$
    ///   - \c what=4 \f$ \frac{\partial \epsilon}{\partial \sigma_{\beta \beta}} \f$
    ///
    /// @param[in] t The input densities and derivatives as required by the functional
    /// @param[in] what Specifies which component of the potential is to be computed as described above
    /// @return The component specified by the \c what parameter
    madness::Tensor<double> vxc(const std::vector< madness::Tensor<double> >& t, const int ispin, const int what) const;

    /// Crude function to plot the energy and potential functionals
    void plot() const {
        long npt = 1001;
        double lo=1e-6, hi=1e+1, s=std::pow(hi/lo, 1.0/(npt-1));

        madness::Tensor<double> rho(npt);
        for (int i=0; i<npt; i++) {
            rho[i] = lo;
            lo *= s;
        }
        std::vector< madness::Tensor<double> > t;
        t.push_back(rho);
        if (is_spin_polarized()) t.push_back(rho);
        madness::Tensor<double> f  = exc(t,0); //pending UGHHHHH
        madness::Tensor<double> va = vxc(t,0,0);
        madness::Tensor<double> vb = vxc(t,0,1);
        for (long i=0; i<npt; i++) {
            printf("%.3e %.3e %.3e %.3e\n", rho[i], f[i], va[i], vb[i]);
        }
    }
};

/// Class to compute the energy functional
struct xc_functional {
    const XCfunctional* xc;
    const int ispin;

    xc_functional(const XCfunctional& xc, int ispin)
        : xc(&xc), ispin(ispin)
    {}

    madness::Tensor<double> operator()(const madness::Key<3> & key, const std::vector< madness::Tensor<double> >& t) const
    {
        MADNESS_ASSERT(xc);
        return xc->exc(t,ispin);
    }
};

/// Class to compute terms of the potential
struct xc_potential {
    const XCfunctional* xc;
    const int what;
    const int ispin;

    xc_potential(const XCfunctional& xc, int ispin,int what)
        : xc(&xc), what(what), ispin(ispin)
    {}

    madness::Tensor<double> operator()(const madness::Key<3> & key, const std::vector< madness::Tensor<double> >& t) const
    {
        MADNESS_ASSERT(xc);
        madness::Tensor<double> r = xc->vxc(t, ispin, what);
        return r;
    }
};

#endif
