/*
  This file is part of MADNESS.

  Copyright (C) 2007,2010 Oak Ridge National Laboratory

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

  For more information please contact:

  Robert J. Harrison
  Oak Ridge National Laboratory
  One Bethel Valley Road
  P.O. Box 2008, MS-6367

  email: harrisonrj@ornl.gov
  tel:   865-241-3937
  fax:   865-572-0680

  $Id$
*/


#ifndef MADNESS_MRA_FUNCTION_FACTORY_H__INCLUDED
#define MADNESS_MRA_FUNCTION_FACTORY_H__INCLUDED

#include <tensor/tensor.h>
#include "tensor/gentensor.h"
#include <mra/key.h>
#include <mra/function_interface.h>


/// Holds machinery to set up Functions/FuncImpls using various Factories and Interfaces

/// We provide an abstract base class FunctionFunctorInterface, of which we derive
/// (as of now) the following classes:
///  - ElementaryInterface (formerly FunctorInterfaceWrapper) to wrap elementary functions
///  - ElectronRepulsionInterface to provide 1/r12, which is not elementarily accessible
///  - CompositeFunctionInterface to provide on-demand coefficients of pair functions
///
/// Each of these Interfaces can be used in a FunctionFactory to set up a Function
namespace madness {

	// needed for the CompositeFactory
	template<typename T, std::size_t NDIM>
	class FunctionImpl;

	template<typename T, std::size_t NDIM>
	class Function;

	template<typename T, std::size_t NDIM>
	class AST;

    template<typename T, std::size_t NDIM>
    Tensor<T> fcube(const Key<NDIM>&, T (*f)(const Vector<double,NDIM>&), const Tensor<double>&);

	/// FunctionFactory implements the named-parameter idiom for Function

	/// C++ does not provide named arguments (as does, e.g., Python).
	/// This class provides something very close.  Create functions as follows
	/// \code
	/// double myfunc(const double x[]);
	/// Function<double,3> f = FunctionFactory<double,3>(world).f(myfunc).k(11).thresh(1e-9).debug()
	/// \endcode
	/// where the methods of function factory, which specify the non-default
	/// arguments eventually passed to the \c Function constructor, can be
	/// used in any order.
	///
	/// Need to add a general functor for initial projection with a standard interface.
	template<typename T, std::size_t NDIM>
	class FunctionFactory {
		friend class FunctionImpl<T, NDIM> ;
		typedef Vector<double, NDIM> coordT; ///< Type of vector holding coordinates

	protected:
		World& _world;
		int _k;
		double _thresh;
		int _initial_level;
		int _max_refine_level;
		int _truncate_mode;
		bool _refine;
		bool _empty;
		bool _autorefine;
		bool _truncate_on_project;
		bool _fence;
		bool _is_on_demand;
		
		//Tensor<int> _bc;
		std::shared_ptr<WorldDCPmapInterface<Key<NDIM> > > _pmap;

	private:
		// need to keep this private, access only via get_functor();
		// reason is that the functor must only be constructed when the actual
		// FuncImpl is constructed, otherwise we might depend on the ordering
		// of the chaining (specifically, if the functor is constructed before
		// of after the threshold is changed)
		std::shared_ptr<FunctionFunctorInterface<T, NDIM> > _functor;

	public:

		FunctionFactory(World& world) :
				_world(world),
				_k(FunctionDefaults<NDIM>::get_k()),
				_thresh(FunctionDefaults<NDIM>::get_thresh()),
				_initial_level(
					FunctionDefaults<NDIM>::get_initial_level()),
				_max_refine_level(
					FunctionDefaults<NDIM>::get_max_refine_level()),
				_truncate_mode(
					FunctionDefaults<NDIM>::get_truncate_mode()),
				_refine(FunctionDefaults<NDIM>::get_refine()),
				_empty(false),
				_autorefine(FunctionDefaults<NDIM>::get_autorefine()),
				_truncate_on_project(
					FunctionDefaults<NDIM>::get_truncate_on_project()),
				_fence(true), // _bc(FunctionDefaults<NDIM>::get_bc()),
				_is_on_demand(false),
				    _pmap(FunctionDefaults<NDIM>::get_pmap()), _functor()
				     {
				    
		}

		virtual ~FunctionFactory() {};
		FunctionFactory&
		functor(
			const std::shared_ptr<FunctionFunctorInterface<T, NDIM> >& f) {
			_functor = f;
			return self();
		}
		template<typename opT>
		FunctionFactory&
        functor2(const opT& op) {
            _functor=std::shared_ptr<FunctionInterface<T,NDIM,opT> >
            (new FunctionInterface<T,NDIM,opT>(op));
			return self();
     }

		FunctionFactory&
		no_functor() {
			_functor.reset();
			return self();
		}
		FunctionFactory&
		f(T
		  (*f)(const coordT&)) {
			functor(std::shared_ptr<ElementaryInterface<T, NDIM> > (
						new ElementaryInterface<T,NDIM>(f)));
			return self();
		}

		virtual FunctionFactory& k(int k) {
			_k = k;
			return self();
		}

		virtual FunctionFactory& thresh(double thresh) {
			_thresh = thresh;
			return self();
		}

		FunctionFactory&
		initial_level(int initial_level) {
			_initial_level = initial_level;
			return self();
		}

		FunctionFactory&
		max_refine_level(int max_refine_level) {
			_max_refine_level = max_refine_level;
			return self();
		}
		FunctionFactory&
		truncate_mode(int truncate_mode) {
			_truncate_mode = truncate_mode;
			return self();
		}
		FunctionFactory&
		refine(bool refine = true) {
			_refine = refine;
			return self();
		}
		FunctionFactory&
		norefine(bool norefine = true) {
			_refine = !norefine;
			return self();
		}
		FunctionFactory&
		empty() {
			_empty = true;
			return self();
		}
		FunctionFactory&
		autorefine() {
			_autorefine = true;
			return self();
		}
		FunctionFactory&
		noautorefine() {
			_autorefine = false;
			return self();
		}
		FunctionFactory&
		truncate_on_project() {
			_truncate_on_project = true;
			return self();
		}
		FunctionFactory&
		notruncate_on_project() {
			_truncate_on_project = false;
			return self();
		}
		FunctionFactory&
		fence(bool fence = true) {
			_fence = fence;
			return self();
		}
		FunctionFactory&
		nofence() {
			_fence = false;
			return self();
		}
		virtual FunctionFactory&
		is_on_demand() {
			_is_on_demand = true;
			return self();
		}
		FunctionFactory&
		pmap(const std::shared_ptr<WorldDCPmapInterface<Key<NDIM> > >& pmap) {
			_pmap = pmap;
			return self();
		}

		int get_k() const {return _k;};
		double get_thresh() const {return _thresh;};
		World& get_world() const {return _world;};

		/// return the functor; override this if the functor needs deferred construction
		virtual std::shared_ptr<FunctionFunctorInterface<T, NDIM> > get_functor() const {
    		return _functor;
    	}

		/// implement this in all derived classes for correct chaining
		FunctionFactory& self() {return *this;}

	};


    /// Factory for facile setup of a CompositeFunctorInterface and its FuncImpl

    /// for the concept of a Factory see base class FunctionFactory
    /// here we need to provide two different dimensions, since the main purpose
    /// of this is to set up a pair function (6D), consisting of orbitals (3D),
    /// and also one- and two-electron potentials
	///
	/// This Factory constructs a FuncImpl, and also the functor to it.
	///
	/// NOTE: pass in only copies of functions, since use in here will corrupt the
	/// tree structure and functions will not pass the VERIFY test after this.
    template<typename T, std::size_t NDIM, std::size_t MDIM>
    class CompositeFactory : public FunctionFactory<T, NDIM> {
    public:
    	std::shared_ptr<FunctionImpl<T,NDIM> > _ket;		///< supposedly a 6D pair function ket
    	std::shared_ptr<FunctionImpl<T,NDIM> > _g12;		///< supposedly a interaction potential
    	std::shared_ptr<FunctionImpl<T,MDIM> > _v1; 		///< supposedly a potential for particle 1
    	std::shared_ptr<FunctionImpl<T,MDIM> > _v2; 		///< supposedly a potential for particle 2
    	std::shared_ptr<FunctionImpl<T,MDIM> > _particle1; 	///< supposedly particle 1
    	std::shared_ptr<FunctionImpl<T,MDIM> > _particle2; 	///< supposedly particle 2

    private:
    	std::shared_ptr<CompositeFunctorInterface<T, NDIM, MDIM> > _func;

    	friend class CompositeFunctorInterface<T, NDIM, MDIM>;

    public:

    	CompositeFactory(World& world)
    		: FunctionFactory<T,NDIM>(world)
    		, _ket()
    		, _g12()
    		, _v1()
    		, _v2()
    		, _particle1()
			, _particle2()
			, _func() {

    		// there are certain defaults that make only sense here
    		this->_is_on_demand=true;
    	}

    	/// provide directly the NDIM (6D) pair function ket
        CompositeFactory&
//        ket(const std::shared_ptr<FunctionImpl<T, NDIM> >& f) {
        ket(const Function<T, NDIM>& f) {
        	_ket = f.get_impl();
			return self();
        }

        /// g12 is the interaction potential (6D)
        CompositeFactory&
        g12(const Function<T, NDIM>& f) {
        	_g12 = f.get_impl();
			return self();
        }

        /// a one-particle potential, acting on particle 1
        CompositeFactory&
        V_for_particle1(const Function<T, MDIM>& f) {
        	_v1 = f.get_impl();
			return self();
        }

        /// a one-particle potential, acting on particle 2
        CompositeFactory&
        V_for_particle2(const Function<T, MDIM>& f) {
        	_v2 = f.get_impl();
			return self();
        }

        /// provide particle 1, used with particle 2 to set up a pair function by
        /// direct product
        CompositeFactory&
        particle1(const Function<T, MDIM>& f) {
        	_particle1 = f.get_impl();
			return self();
        }

        /// provide particle 2, used with particle 1 to set up a pair function by
        /// direct product
        CompositeFactory&
        particle2(const Function<T, MDIM>& f) {
        	_particle2 = f.get_impl();
			return self();
        }

    	// access to the functor *only* via this
    	std::shared_ptr<FunctionFunctorInterface<T, NDIM> > get_functor() const {

    		// return if we already have a valid functor
    		if (this->_func) return this->_func;

    		// construction of the functor is const in spirit, but non-const in sad reality..
    		// this Factory not only constructs the Function, but also the functor, so
    		// pass *this to the interface
    		const_cast< std::shared_ptr<CompositeFunctorInterface<T, NDIM, MDIM> >& >(this->_func)=
    				std::shared_ptr<CompositeFunctorInterface<T, NDIM, MDIM> >(
    				new CompositeFunctorInterface<double, NDIM, MDIM>(
    						this->_world,_ket,_g12,_v1,_v2,_particle1,_particle2
    						));

    		return this->_func;
    	}

    	CompositeFactory& self() {return *this;}
    };

    /// factory for generating TwoElectronInterfaces
    class TwoElectronFactory : public FunctionFactory<double,6> {

    protected:
    	typedef std::shared_ptr<FunctionFunctorInterface<double, 6> > InterfacePtr;

    public:
    	TwoElectronFactory(World& world)
    		: FunctionFactory(world)
    		, type_(coulomb_)
    		, interface_()
			, dcut_(FunctionDefaults<3>::get_thresh())
    		, gamma_(-1.0)
			, bc_(FunctionDefaults<6>::get_bc()) {
    		_is_on_demand=true;
    		this->_thresh=(FunctionDefaults<3>::get_thresh());
    		this->_k=(FunctionDefaults<3>::get_k());

    	}

    	/// the smallest length scale to be represented (aka lo)
    	TwoElectronFactory& dcut(double dcut) {
    		dcut_=dcut;
    		return self();
    	}

    	/// the requested precision
    	TwoElectronFactory& thresh(double thresh) {
    		_thresh=thresh;
    		return self();
    	}

    	/// the exponent of a slater function
    	TwoElectronFactory& gamma(double g) {
    		gamma_=g;
    		return self();
    	}

    	/// return the operator  (1 - exp(-gamma x) / (2 gamma)
    	TwoElectronFactory& f12() {
    		type_=f12_;
    		return self();
    	}

    	/// return the operator  (1 - exp(-gamma x) / (2 gamma)
    	TwoElectronFactory& slater() {
    		type_=slater_;
    		return self();
    	}

    	// access to the functor *only* via this
    	InterfacePtr get_functor() const {

    		// return if we already have a valid interface
    		if (this->interface_) return this->interface_;

    		// construction of the functor is const in spirit, but non-const in sad reality..
    		if (type_==coulomb_) {
    			const_cast<InterfacePtr& >(this->interface_)=
    				InterfacePtr(new ElectronRepulsionInterface(
    						dcut_,_thresh,bc_,_k));
    		} else if (type_==f12_) {
    			// make sure gamma is set
    			MADNESS_ASSERT(gamma_>0);
    			const_cast<InterfacePtr& >(this->interface_)=
    				InterfacePtr(new SlaterF12Interface(
    						gamma_,dcut_,_thresh,bc_,_k));
    		} else if (type_==slater_) {
    			// make sure gamma is set
    			MADNESS_ASSERT(gamma_>0);
    			const_cast<InterfacePtr& >(this->interface_)=
    				InterfacePtr(new SlaterFunctionInterface(
    						gamma_,dcut_,_thresh,bc_,_k));
    		} else {
    			MADNESS_EXCEPTION("unimplemented integral kernel",1);
    		}
    		return this->interface_;
    	}

    	TwoElectronFactory& self() {return *this;}

    protected:

    	enum operatortype {coulomb_, slater_, f12_, bsh_, fg_};

    	operatortype type_;

    	/// the interface providing the actual coefficients
    	InterfacePtr interface_;

    	double dcut_;		///< cutoff radius for 1/r12, aka regularization

    	double gamma_;

        BoundaryConditions<6> bc_;

    };

#if 0
    class ERIFactory : public TwoElectronFactory<ERIFactory> {
    public:
    	ERIFactory(World& world) : TwoElectronFactory<ERIFactory>(world) {}

    	// access to the functor *only* via this
    	InterfacePtr get_functor() const {

    		// return if we already have a valid interface
    		if (this->interface_) return this->interface_;

    		// construction of the functor is const in spirit, but non-const in sad reality..
    		const_cast<InterfacePtr& >(this->interface_)=
    				InterfacePtr(new ElectronRepulsionInterface(
    						dcut_,thresh_,bc_,k_));
    		return this->interface_;
    	}

    	ERIFactory& self() {return *this;}

    };

	/// a function like f(x) = 1 - exp(-mu x)
    class SlaterFunctionFactory : public TwoElectronFactory<SlaterFunctionFacto> {
    public:
    	SlaterFunctionFactory(World& world)
    		: TwoElectronFactory(world), gamma_(-1.0), f12_(false) {}

    	/// set the exponent of the Slater function
    	SlaterFunctionFactory& gamma(double gamma) {
			this->gamma_ = gamma;
			return self();
		}

    	/// do special f12 function
    	SlaterFunctionFactory& f12() {
			this->f12_=true;
			return self();
		}

    	// access to the functor *only* via this
    	InterfacePtr get_functor() const {

    		// return if we already have a valid interface
    		if (this->interface_) return this->interface_;

    		// make sure gamma is set
    		MADNESS_ASSERT(gamma_>0);

    		// construction of the functor is const in spirit, but non-const in sad reality..
    		if (f12_) {
    			const_cast<InterfacePtr& >(this->interface_)=
    				InterfacePtr(new SlaterF12Interface(
    						gamma_,dcut_,this->_thresh,bc_,this->_k));
    		} else {
    			const_cast<InterfacePtr& >(this->interface_)=
    				InterfacePtr(new SlaterFunctionInterface(
    						gamma_,dcut_,this->_thresh,bc_,this->_k));
    		}
    		return this->interface_;
    	}

    	SlaterFunctionFactory& self() {return *this;}

    private:

    	double gamma_;		///< the exponent of the Slater function f(x)=exp(-gamma x)
    	bool f12_;			///< use 1-exp(-gamma x)  instead of exp(-gamma x)
    };

    /// Factory to set up an ElectronRepulsion Function
    template<typename T, std::size_t NDIM>
    class ERIFactory : public FunctionFactory<T, NDIM> {

    private:
    	std::shared_ptr<ElectronRepulsionInterface> _eri;

    public:

    	/// cutoff radius for 1/r12, aka regularization
    	double _dcut;
        BoundaryConditions<NDIM> _bc;

    public:
    	ERIFactory(World& world)
    		: FunctionFactory<T,NDIM>(world)
    		, _eri()
    		, _dcut(FunctionDefaults<NDIM>::get_thresh())
    		, _bc(FunctionDefaults<NDIM>::get_bc())
    	{
    		this->_is_on_demand=true;
    		MADNESS_ASSERT(NDIM==6);
    	}

    	ERIFactory&
		thresh(double thresh) {
			this->_thresh = thresh;
			return *this;
		}

    	ERIFactory&
		dcut(double dcut) {
			this->_dcut = dcut;
			return *this;
		}

    	// access to the functor *only* via this
    	std::shared_ptr<FunctionFunctorInterface<T, NDIM> > get_functor() const {

    		// return if we already have a valid eri
    		if (this->_eri) return this->_eri;

//    		if (this->_world.rank()==0) print("set dcut in ERIFactory to ", _dcut);

    		// construction of the functor is const in spirit, but non-const in sad reality..
    		const_cast< std::shared_ptr<ElectronRepulsionInterface>& >(this->_eri)=
    				std::shared_ptr<ElectronRepulsionInterface>(
    				new ElectronRepulsionInterface(_dcut,this->_thresh,
    		                _bc,this->_k));

    		return this->_eri;
    	}

    };
#endif

    /// Factory to set up an ElectronRepulsion Function
    template<typename T, std::size_t NDIM>
    class FGFactory : public FunctionFactory<T, NDIM> {

    private:
    	std::shared_ptr<FGInterface> _fg;

    public:

    	/// cutoff radius for 1/r12, aka regularization
    	double _dcut;
    	double _gamma;
        BoundaryConditions<NDIM> _bc;

    public:
        FGFactory(World& world, double gamma)
    		: FunctionFactory<T,NDIM>(world)
    		, _fg()
    		, _dcut(FunctionDefaults<NDIM>::get_thresh())
    		, _gamma(gamma)
    		, _bc(FunctionDefaults<NDIM>::get_bc())
    	{
    		this->_is_on_demand=true;
    		MADNESS_ASSERT(NDIM==6);
    	}

        FGFactory&
		thresh(double thresh) {
			this->_thresh = thresh;
			return *this;
		}

        FGFactory&
		dcut(double dcut) {
			this->_dcut = dcut;
			return *this;
		}

    	// access to the functor *only* via this
    	std::shared_ptr<FunctionFunctorInterface<T, NDIM> > get_functor() const {

    		// return if we already have a valid eri
    		if (this->_fg) return this->_fg;

//    		if (this->_world.rank()==0) print("set dcut in ERIFactory to ", _dcut);

    		// construction of the functor is const in spirit, but non-const in sad reality..
    		const_cast< std::shared_ptr<FGInterface>& >(this->_fg)=
    				std::shared_ptr<FGInterface>(
    				new FGInterface(this->_world,_dcut,this->_thresh,
    		                _gamma,_bc,this->_k));

    		return this->_fg;
    	}

    };

}

#endif // MADNESS_MRA_FUNCTION_FACTORY_H__INCLUDED
