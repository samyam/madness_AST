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

/// \file SCF.cc
/// \brief Molecular HF and DFT code
/// \defgroup moldft The molecular density funcitonal and Hartree-Fock code


#define WORLD_INSTANTIATE_STATIC_TEMPLATES


#include <chem/SCF.h>

#include <tensor/elem.h>
#include <TAU.h>
#include <mra/lbdeux.h>
#include <mra/qmprop.h>
#include <misc/misc.h>
#include <misc/ran.h>

namespace madness {
template<int NDIM>
struct unaryexp {
    void operator()(const Key<NDIM>& key, Tensor<double_complex>& t) const {
        //vzExp(t.size, t.ptr(), t.ptr());
        UNARY_OPTIMIZED_ITERATOR(double_complex, t, *_p0 = exp(*_p0););
    }
    template <typename Archive>
    void serialize(Archive& ar) {}
};




static double rsquared(const coordT& r) {
    return r[0]*r[0] + r[1]*r[1] + r[2]*r[2];
}

// Returns exp(-I*t*V)
static Function<double_complex,3> make_exp(double t, const Function<double,3>& v) {
    v.reconstruct();
    Function<double_complex,3> expV = double_complex(0.0,-t)*v;
    expV.unaryop(unaryexp<3>());
    //expV.truncate(); expV.reconstruct();
    return expV;
}

static double ttt, sss;
static void START_TIMER(World& world) {
    world.gop.fence(); ttt=wall_time(); sss=cpu_time();
}

static void END_TIMER(World& world, const char* msg) {
    ttt=wall_time()-ttt; sss=cpu_time()-sss; if (world.rank()==0) printf("timer: %20.20s %8.2fs %8.2fs\n", msg, sss, ttt);
}

extern void drot(long n, double* restrict a, double* restrict b, double s, double c, long inc);


/// Given overlap matrix, return rotation with 3rd order error to orthonormalize the vectors

tensorT Q3(const tensorT& s) {
    tensorT Q = inner(s,s);
    Q.gaxpy(0.2,s,-2.0/3.0);
    for (int i=0; i<s.dim(0); ++i) Q(i,i) += 1.0;
    return Q.scale(15.0/8.0);
}



void drot3(long n, double* restrict a, double* restrict b, double s, double c, long inc) {
    if (inc == 1) {
        n*=3;
        for (long i=0; i<n; i+=3) {
            double aa0 = a[i  ]*c - b[i  ]*s;
            double bb0 = b[i  ]*c + a[i  ]*s;
            double aa1 = a[i+1]*c - b[i+1]*s;
            double bb1 = b[i+1]*c + a[i+1]*s;
            double aa2 = a[i+2]*c - b[i+2]*s;
            double bb2 = b[i+2]*c + a[i+2]*s;
            a[i  ] = aa0;
            b[i  ] = bb0;
            a[i+1] = aa1;
            b[i+1] = bb1;
            a[i+2] = aa2;
            b[i+2] = bb2;
        }
    }
    else {
        inc*=3;
        n*=inc;
        for (long i=0; i<n; i+=inc) {
            double aa0 = a[i  ]*c - b[i  ]*s;
            double bb0 = b[i  ]*c + a[i  ]*s;
            double aa1 = a[i+1]*c - b[i+1]*s;
            double bb1 = b[i+1]*c + a[i+1]*s;
            double aa2 = a[i+2]*c - b[i+2]*s;
            double bb2 = b[i+2]*c + a[i+2]*s;
            a[i  ] = aa0;
            b[i  ] = bb0;
            a[i+1] = aa1;
            b[i+1] = bb1;
            a[i+2] = aa2;
            b[i+2] = bb2;
        }
    }
}

SCF::SCF(World & world, const char *filename) {
	TAU_START("Calculation (World &, const char *");
	if (world.rank() == 0) {
		molecule.read_file(filename);
		param.read_file(filename);
		unsigned int n_core = 0;
		if (param.core_type != "") {
			molecule.read_core_file(param.core_type);
			param.aobasis = molecule.guess_file();
			n_core = molecule.n_core_orb_all();
		}

		molecule.orient();
		aobasis.read_file(param.aobasis);
		param.set_molecular_info(molecule, aobasis, n_core);
	}
	world.gop.broadcast_serializable(molecule, 0);
	world.gop.broadcast_serializable(param, 0);
	world.gop.broadcast_serializable(aobasis, 0);

	TAU_START("xc.initialize");
	xc.initialize(param.xc_data, !param.spin_restricted);
	TAU_STOP("xc.initialize");
	//xc.plot();

	FunctionDefaults < 3 > ::set_cubic_cell(-param.L, param.L);
	set_protocol < 3 > (world, param.econv);

	potentialmanager = std::shared_ptr < PotentialManager
			> (new PotentialManager(molecule, param.core_type));
	TAU_STOP("Calculation (World &, const char *");
}


void SCF::save_mos(World& world) {
	TAU_START("archive::ParallelOutputArchive ar(world)");
	archive::ParallelOutputArchive ar(world, "restartdata", param.nio);
	TAU_STOP("archive::ParallelOutputArchive ar(world)");
	ar & current_energy & param.spin_restricted;
	ar & (unsigned int) (amo.size());
	ar & aeps & aocc & aset;
	for (unsigned int i = 0; i < amo.size(); ++i)
		ar & amo[i];
	if (!param.spin_restricted) {
		ar & (unsigned int) (bmo.size());
		ar & beps & bocc & bset;
		for (unsigned int i = 0; i < bmo.size(); ++i)
			ar & bmo[i];
	}
}

void SCF::load_mos(World& world) {
	TAU_START("load_mos");
	//        const double trantol = vtol / std::min(30.0, double(param.nalpha));
	const double thresh = FunctionDefaults < 3 > ::get_thresh();
	const int k = FunctionDefaults < 3 > ::get_k();
	unsigned int nmo = 0;
	bool spinrest = false;
	amo.clear();
	bmo.clear();

	archive::ParallelInputArchive ar(world, "restartdata");

	/*
	 File format:

	 bool spinrestricted --> if true only alpha orbitals are present

	 unsigned int nmo_alpha;
	 Tensor<double> aeps;
	 Tensor<double> aocc;
	 vector<int> aset;
	 for i from 0 to nalpha-1:
	 .   Function<double,3> amo[i]

	 repeat for beta if !spinrestricted

	 */

	// LOTS OF LOGIC MISSING HERE TO CHANGE OCCUPATION NO., SET,
	// EPS, SWAP, ... sigh
	ar & current_energy & spinrest;

	ar & nmo;
	MADNESS_ASSERT(nmo >= unsigned(param.nmo_alpha));
	ar & aeps & aocc & aset;
	amo.resize(nmo);
	for (unsigned int i = 0; i < amo.size(); ++i)
		ar & amo[i];
	unsigned int n_core = molecule.n_core_orb_all();
	if (nmo > unsigned(param.nmo_alpha)) {
		aset = vector<int>(aset.begin() + n_core,
				aset.begin() + n_core + param.nmo_alpha);
		amo = vecfuncT(amo.begin() + n_core,
				amo.begin() + n_core + param.nmo_alpha);
		aeps = copy(aeps(Slice(n_core, n_core + param.nmo_alpha - 1)));
		aocc = copy(aocc(Slice(n_core, n_core + param.nmo_alpha - 1)));
	}

	if (amo[0].k() != k) {
		reconstruct(world, amo);
		for (unsigned int i = 0; i < amo.size(); ++i)
			amo[i] = madness::project(amo[i], k, thresh, false);
		world.gop.fence();
	}
	//        normalize(world, amo);
	//        amo = transform(world, amo, Q3(matrix_inner(world, amo, amo)), trantol, true);
	//        truncate(world, amo);
	//        normalize(world, amo);

	if (!param.spin_restricted) {

		if (spinrest) { // Only alpha spin orbitals were on disk
			MADNESS_ASSERT(param.nmo_alpha >= param.nmo_beta);
			bmo.resize(param.nmo_beta);
			bset.resize(param.nmo_beta);
			beps = copy(aeps(Slice(0, param.nmo_beta - 1)));
			bocc = copy(aocc(Slice(0, param.nmo_beta - 1)));
			for (int i = 0; i < param.nmo_beta; ++i)
				bmo[i] = copy(amo[i]);
		} else {
			ar & nmo;
			ar & beps & bocc & bset;

			bmo.resize(nmo);
			for (unsigned int i = 0; i < bmo.size(); ++i)
				ar & bmo[i];

			if (nmo > unsigned(param.nmo_beta)) {
				bset = vector<int>(bset.begin() + n_core,
						bset.begin() + n_core + param.nmo_beta);
				bmo = vecfuncT(bmo.begin() + n_core,
						bmo.begin() + n_core + param.nmo_beta);
				beps = copy(beps(Slice(n_core, n_core + param.nmo_beta - 1)));
				bocc = copy(bocc(Slice(n_core, n_core + param.nmo_beta - 1)));
			}

			if (bmo[0].k() != k) {
				reconstruct(world, bmo);
				for (unsigned int i = 0; i < bmo.size(); ++i)
					bmo[i] = madness::project(bmo[i], k, thresh, false);
				world.gop.fence();
			}

			//                normalize(world, bmo);
			//                bmo = transform(world, bmo, Q3(matrix_inner(world, bmo, bmo)), trantol, true);
			//                truncate(world, bmo);
			//                normalize(world, bmo);

		}
	}
	TAU_STOP("load_mos");
}

void SCF::do_plots(World& world) {
	START_TIMER(world);
	TAU_START("do_plots");

	std::vector<long> npt(3, param.npt_plot);

	if (param.plot_cell.size() == 0)
		param.plot_cell = copy(FunctionDefaults < 3 > ::get_cell());

	if (param.plotdens || param.plotcoul) {
		functionT rho;
		rho = make_density(world, aocc, amo);

		if (param.spin_restricted) {
			rho.scale(2.0);
		} else {
			functionT rhob = make_density(world, bocc, bmo);
			functionT rho_spin = rho - rhob;
			rho += rhob;
			plotdx(rho_spin, "spin_density.dx", param.plot_cell, npt, true);

		}
		plotdx(rho, "total_density.dx", param.plot_cell, npt, true);
		if (param.plotcoul) {
			real_function_3d vnuc = potentialmanager->vnuclear();
			functionT vlocl = vnuc + apply(*coulop, rho);
			vlocl.truncate();
			vlocl.reconstruct();
			plotdx(vlocl, "coulomb.dx", param.plot_cell, npt, true);
		}
	}

	for (int i = param.plotlo; i <= param.plothi; ++i) {
		char fname[256];
		if (i < param.nalpha) {
			sprintf(fname, "amo-%5.5d.dx", i);
			plotdx(amo[i], fname, param.plot_cell, npt, true);
		}
		if (!param.spin_restricted && i < param.nbeta) {
			sprintf(fname, "bmo-%5.5d.dx", i);
			plotdx(bmo[i], fname, param.plot_cell, npt, true);
		}
	}
	END_TIMER(world, "plotting");
	TAU_STOP("do_plots");
}

void SCF::project(World & world) {
	reconstruct(world, amo);
	for (unsigned int i = 0; i < amo.size(); ++i) {
		amo[i] = madness::project(amo[i], FunctionDefaults < 3 > ::get_k(),
				FunctionDefaults < 3 > ::get_thresh(), false);
	}
	world.gop.fence();
	truncate(world, amo);
	normalize(world, amo);
	if (param.nbeta && !param.spin_restricted) {
		reconstruct(world, bmo);
		for (unsigned int i = 0; i < bmo.size(); ++i) {
			bmo[i] = madness::project(bmo[i], FunctionDefaults < 3 > ::get_k(),
					FunctionDefaults < 3 > ::get_thresh(), false);
		}
		world.gop.fence();
		truncate(world, bmo);
		normalize(world, bmo);
	}
}

void SCF::make_nuclear_potential(World & world) {
	TAU_START("Project vnuclear");
	START_TIMER(world);
	potentialmanager->make_nuclear_potential(world);
	END_TIMER(world, "Project vnuclear");
	TAU_STOP("Project vnuclear");
}

void SCF::project_ao_basis(World & world) {
	// Make at_to_bf, at_nbf ... map from atom to first bf on atom, and nbf/atom
	aobasis.atoms_to_bfn(molecule, at_to_bf, at_nbf);

	TAU_START("project ao basis");
	START_TIMER(world);
	ao = vecfuncT(aobasis.nbf(molecule));
	for (int i = 0; i < aobasis.nbf(molecule); ++i) {
		functorT aofunc(
				new AtomicBasisFunctor(
						aobasis.get_atomic_basis_function(molecule, i)));
		ao[i] =
				factoryT(world).functor(aofunc).truncate_on_project().nofence().truncate_mode(
						1);
	}
	world.gop.fence();
	truncate(world, ao);
	normalize(world, ao);
	END_TIMER(world, "project ao basis");
	TAU_STOP("project ao basis");
	print_meminfo(world.rank(), "project ao basis");
}

double SCF::PM_q(const tensorT & S, const double * restrict Ci, const double * restrict Cj,
		int lo, int nbf) {
	double qij = 0.0;
	if (nbf == 1) { // H atom in STO-3G ... often lots of these!
		qij = Ci[lo]*S(0,0)*Cj[lo];
	}
	else {
		for(int mu = 0;mu < nbf;++mu) {
			double Smuj = 0.0;
			for(int nu = 0;nu < nbf;++nu) {
				Smuj += S(mu, nu) * Cj[nu + lo];
			}
			qij += Ci[mu + lo] * Smuj;
		}
	}

	return qij;
}

void SCF::localize_PM_ij(const int seti, const int setj,
		const double tol, const double thetamax,
		const int natom, const int nao, const int nmo,
		const std::vector<tensorT>& Svec,
		const std::vector<int>& at_to_bf, const std::vector<int>& at_nbf,
		long& ndone_iter, double& maxtheta,
		double * restrict Qi, double * restrict Qj,
		double * restrict Ci, double * restrict Cj,
		double * restrict Ui, double * restrict Uj)
{
	if(seti == setj) {
		// Q could be recomputed for each ij, but by saving it we reduce computation by a factor
		// of 2 when far from convergence, and as we approach convergence we save more since
		// most work is associated with computing ovij.

		double ovij = 0.0;
		for(long a = 0;a < natom;++a)
		ovij += Qi[a] * Qj[a];

		print("ovij", ovij);
		if(fabs(ovij) > tol * tol) {
			double aij = 0.0;
			double bij = 0.0;
			for(long a = 0;a < natom;++a) {
				double qiia = Qi[a];
				double qija = PM_q(Svec[a], Ci, Cj, at_to_bf[a], at_nbf[a]);
				double qjja = Qj[a];
				double d = qiia - qjja;
				aij += qija * qija - 0.25 * d * d;
				bij += qija * d;
			}
			double theta = 0.25 * acos(-aij / sqrt(aij * aij + bij * bij));

			if(bij > 0.0)
			theta = -theta;

			print("theta", theta);
			if(theta > thetamax)
			theta = thetamax;
			else
			if(theta < -thetamax)
			theta = -thetamax;

			maxtheta = std::max(fabs(theta), maxtheta);
			if(fabs(theta) >= tol) {
				++ndone_iter;
				double c = cos(theta);
				double s = sin(theta);
				print(c,s);
				drot(nao, Ci, Cj, s, c, 1);
				drot(nmo, Ui, Uj, s, c, 1);
				for(long a = 0;a < natom;++a) {
					Qi[a] = PM_q(Svec[a], Ci, Ci, at_to_bf[a], at_nbf[a]);
					Qj[a] = PM_q(Svec[a], Cj, Cj, at_to_bf[a], at_nbf[a]);
				}
			}
		}
	}
}

void SCF::localize_PM_task_kernel(tensorT & Q, std::vector<tensorT> & Svec,
		tensorT & C, const bool & doprint, const std::vector<int> & set,
		const double thetamax, tensorT & U, const double thresh) {
	long nmo = C.dim(0);
	long nao = C.dim(1);
	long natom = molecule.natom();

	for (long i = 0; i < nmo; ++i) {
		for (long a = 0; a < natom; ++a) {
			Q(i, a) = PM_q(Svec[a], &C(i, 0), &C(i, 0), at_to_bf[a], at_nbf[a]);
		}
	}

	print("Q\n", Q);

	double tol = 0.1;
	long ndone = 0;
	for (long iter = 0; iter < 100; ++iter) {

		// Diagnostics at beginning of iteration
		double sum = 0.0;
		for (long i = 0; i < nmo; ++i) {
			for (long a = 0; a < natom; ++a) {
				double qiia = Q(i, a);
				sum += qiia * qiia;
			}
		}
		if (doprint)
			printf("iteration %ld sum=%.4f ndone=%ld tol=%.2e\n", iter, sum,
					ndone, tol);
		// End diagnostics at beginning of iteration

		// Initialize variables for convergence test
		long ndone_iter = 0;
		double maxtheta = 0.0;
		for (long i = 0; i < nmo; ++i) {
			for (long j = 0; j < i; ++j) {

				localize_PM_ij(set[i], set[j], tol, thetamax, natom, nao, nmo,
						Svec, at_to_bf, at_nbf, ndone_iter, maxtheta, &Q(i, 0),
						&Q(j, 0), &C(i, 0), &C(j, 0), &U(i, 0), &U(j, 0));

			}
		}

		ndone += ndone_iter;
		if (ndone_iter == 0 && tol == thresh) {
			if (doprint)
				print("PM localization converged in", ndone, "steps");

			break;
		}
		tol = std::max(0.1 * std::min(maxtheta, tol), thresh);
	}
}

tensorT SCF::localize_PM(World & world, const vecfuncT & mo,
		const std::vector<int> & set, const double thresh,
		const double thetamax, const bool randomize,
		const bool doprint) {
	TAU_START("Pipek-Mezy localize");
	START_TIMER(world);
	tensorT UT = distributed_localize_PM(world, mo, ao, set, at_to_bf, at_nbf,
			thresh, thetamax, randomize, doprint);
	END_TIMER(world, "Pipek-Mezy distributed ");
	//print(UT);

	return UT;

	START_TIMER(world);
	long nmo = mo.size();
	long natom = molecule.natom();

	tensorT S = matrix_inner(world, ao, ao, true);
	std::vector < tensorT > Svec(natom);
	for (long a = 0; a < natom; ++a) {
		Slice as(at_to_bf[a], at_to_bf[a] + at_nbf[a] - 1);
		Svec[a] = copy(S(as, as));
	}
	S = tensorT();
	tensorT C = matrix_inner(world, mo, ao);
	tensorT U(nmo, nmo);
	tensorT Q(nmo, natom);
	if (world.rank() == 0) {
		for (long i = 0; i < nmo; ++i)
			U(i, i) = 1.0;

		localize_PM_task_kernel(Q, Svec, C, doprint, set, thetamax, U, thresh);
		U = transpose(U);

		// Fix orbital orders
		bool switched = true;
		while (switched) {
			switched = false;
			for (int i = 0; i < nmo; i++) {
				for (int j = i + 1; j < nmo; j++) {
					if (set[i] == set[j]) {
						double sold = U(i, i) * U(i, i) + U(j, j) * U(j, j);
						double snew = U(i, j) * U(i, j) + U(j, i) * U(j, i);
						if (snew > sold) {
							tensorT tmp = copy(U(_, i));
							U(_, i) = U(_, j);
							U(_, j) = tmp;
							switched = true;
						}
					}
				}
			}
		}

		// Fix phases.
		for (long i = 0; i < nmo; ++i) {
			if (U(i, i) < 0.0)
				U(_, i).scale(-1.0);
		}
	}
	world.gop.broadcast(U.ptr(), U.size(), 0);
	END_TIMER(world, "Pipek-Mezy localize");
	TAU_STOP("Pipek-Mezy localize");
	print_meminfo(world.rank(), "Pipek-Mezy localize");
	return U;
}

void SCF::analyze_vectors(World& world, const vecfuncT & mo, const tensorT& occ,
		const tensorT& energy, const std::vector<int>& set) {
	tensorT Saomo = matrix_inner(world, ao, mo);
	tensorT Saoao = matrix_inner(world, ao, ao, true);
	int nmo = mo.size();
	tensorT rsq, dip(3, nmo);
	{
		functionT frsq = factoryT(world).f(rsquared).initial_level(4);
		rsq = inner(world, mo, mul_sparse(world, frsq, mo, vtol));
		for (int axis = 0; axis < 3; ++axis) {
			functionT fdip = factoryT(world).functor(
					functorT(new DipoleFunctor(axis))).initial_level(4);
			dip(axis, _) = inner(world, mo, mul_sparse(world, fdip, mo, vtol));
			for (int i = 0; i < nmo; ++i)
				rsq(i) -= dip(axis, i) * dip(axis, i);

		}
	}
	tensorT C;

	TAU_START("compute eigen gesv analize vectors");
	START_TIMER(world);
	gesvp(world, Saoao, Saomo, C);
	END_TIMER(world, " compute eigen gesv analize vectors");
	TAU_STOP("compute eigen gesv analize vectors");
	if (world.rank() == 0) {
		C = transpose(C);
		long nmo = mo.size();
		for (long i = 0; i < nmo; ++i) {
			printf("  MO%4ld : ", i);
			if (set.size())
				printf("set=%d : ", set[i]);

			if (occ.size())
				printf("occ=%.2f : ", occ(i));

			if (energy.size())
				printf("energy=%13.8f : ", energy(i));

			printf("center=(%.2f,%.2f,%.2f) : radius=%.2f\n", dip(0, i),
					dip(1, i), dip(2, i), sqrt(rsq(i)));
			aobasis.print_anal(molecule, C(i, _));
		}
	}

}

tensorT SCF::localize_boys(World & world, const vecfuncT & mo,
		const std::vector<int> & set, const double thresh,
		const double thetamax, const bool randomize) {
	TAU_START("Boys localize");
	START_TIMER(world);
	const bool doprint = false;
	long nmo = mo.size();
	tensorT dip(nmo, nmo, 3);
	for (int axis = 0; axis < 3; ++axis) {
		functionT fdip = factoryT(world).functor(
				functorT(new DipoleFunctor(axis))).initial_level(4);
		dip(_, _, axis) = matrix_inner(world, mo,
				mul_sparse(world, fdip, mo, vtol), true);
	}
	tensorT U(nmo, nmo);
	if (world.rank() == 0) {
		for (long i = 0; i < nmo; ++i)
			U(i, i) = 1.0;

		double tol = thetamax;
		long ndone = 0;
		bool converged = false;
		for (long iter = 0; iter < 300; ++iter) {
			double sum = 0.0;
			for (long i = 0; i < nmo; ++i) {
				sum += DIP(dip, i, i, i, i);
			}
			long ndone_iter = 0;
			double maxtheta = 0.0;
			if (doprint)
				printf("iteration %ld sum=%.4f ndone=%ld tol=%.2e\n", iter, sum,
						ndone, tol);

			for (long i = 0; i < nmo; ++i) {
				for (long j = 0; j < i; ++j) {
					if (set[i] == set[j]) {
						double g = DIP(dip, i, j, j, j) - DIP(dip, i, j, i, i);
						double h = 4.0 * DIP(dip, i, j, i, j)
								+ 2.0 * DIP(dip, i, i, j, j)
								- DIP(dip, i, i, i, i) - DIP(dip, j, j, j, j);
						double sij = DIP(dip, i, j, i, j);
						bool doit = false;
						if (h >= 0.0) {
							doit = true;
							if (doprint)
								print("             forcing negative h", i, j,
										h);

							h = -1.0;
						}
						double theta = -g / h;
						maxtheta = std::max<double>(std::abs(theta), maxtheta);
						if (fabs(theta) > thetamax) {
							doit = true;
							if (doprint)
								print("             restricting", i, j);

							if (g < 0)
								theta = -thetamax;

							else
								theta = thetamax * 0.8;

						}
						bool randomized = false;
						if (randomize && iter == 0 && sij > 0.01
								&& fabs(theta) < 0.01) {
							randomized = true;
							if (doprint)
								print("             randomizing", i, j);

							theta += (RandomValue<double>() - 0.5);
						}
						if (fabs(theta) >= tol || randomized || doit) {
							++ndone_iter;
							if (doprint)
								print("     rotating", i, j, theta);

							double c = cos(theta);
							double s = sin(theta);
							drot3(nmo, &dip(i, 0, 0), &dip(j, 0, 0), s, c, 1);
							drot3(nmo, &dip(0, i, 0), &dip(0, j, 0), s, c, nmo);
							drot(nmo, &U(i, 0), &U(j, 0), s, c, 1);
						}
					}
				}
			}

			ndone += ndone_iter;
			if (ndone_iter == 0 && tol == thresh) {
				if (doprint)
					print("Boys localization converged in", ndone, "steps");

				converged = true;
				break;
			}
			tol = std::max(0.1 * maxtheta, thresh);
		}

		if (!converged) {
			print("warning: boys localization did not fully converge: ", ndone);
		}
		U = transpose(U);

		bool switched = true;
		while (switched) {
			switched = false;
			for (int i = 0; i < nmo; i++) {
				for (int j = i + 1; j < nmo; j++) {
					if (set[i] == set[j]) {
						double sold = U(i, i) * U(i, i) + U(j, j) * U(j, j);
						double snew = U(i, j) * U(i, j) + U(j, i) * U(j, i);
						if (snew > sold) {
							tensorT tmp = copy(U(_, i));
							U(_, i) = U(_, j);
							U(_, j) = tmp;
							switched = true;
						}
					}
				}
			}
		}

		// Fix phases.
		for (long i = 0; i < nmo; ++i) {
			if (U(i, i) < 0.0)
				U(_, i).scale(-1.0);
		}

	}

	world.gop.broadcast(U.ptr(), U.size(), 0);
	END_TIMER(world, "Boys localize");
	TAU_STOP("Boys localize");
	return U;
}

tensorT SCF::kinetic_energy_matrix(World & world, const vecfuncT & v) const {
	reconstruct(world, v);
	int n = v.size();
	tensorT r(n, n);
	for (int axis = 0; axis < 3; ++axis) {
		vecfuncT dv = apply(world, *(gradop[axis]), v);
		r += matrix_inner(world, dv, dv, true);
		dv.clear();
	}
	return r.scale(0.5);
}

vecfuncT SCF::core_projection(World & world, const vecfuncT & psi,
		const bool include_Bc) {
	int npsi = psi.size();
	if (npsi == 0)
		return psi;
	int natom = molecule.natom();
	vecfuncT proj = zero_functions<double, 3>(world, npsi);
	tensorT overlap_sum(static_cast<long>(npsi));

	for (int i = 0; i < natom; ++i) {
		Atom at = molecule.get_atom(i);
		unsigned int atn = at.atomic_number;
		unsigned int nshell = molecule.n_core_orb(atn);
		if (nshell == 0)
			continue;
		for (unsigned int c = 0; c < nshell; ++c) {
			unsigned int l = molecule.get_core_l(atn, c);
			int max_m = (l + 1) * (l + 2) / 2;
			nshell -= max_m - 1;
			for (int m = 0; m < max_m; ++m) {
				functionT core = factoryT(world).functor(
						functorT(new CoreOrbitalFunctor(molecule, i, c, m)));
				tensorT overlap = inner(world, core, psi);
				overlap_sum += overlap;
				for (int j = 0; j < npsi; ++j) {
					if (include_Bc)
						overlap[j] *= molecule.get_core_bc(atn, c);
					proj[j] += core.scale(overlap[j]);
				}
			}
		}
		world.gop.fence();
	}
	if (world.rank() == 0)
		print("sum_k <core_k|psi_i>:", overlap_sum);
	return proj;
}

double SCF::core_projector_derivative(World & world, const vecfuncT & mo,
		const tensorT & occ, int atom, int axis) {
	vecfuncT cores, dcores;
	std::vector<double> bc;
	unsigned int atn = molecule.get_atom(atom).atomic_number;
	unsigned int ncore = molecule.n_core_orb(atn);

	// projecting core & d/dx core
	for (unsigned int c = 0; c < ncore; ++c) {
		unsigned int l = molecule.get_core_l(atn, c);
		int max_m = (l + 1) * (l + 2) / 2;
		for (int m = 0; m < max_m; ++m) {
			functorT func = functorT(
					new CoreOrbitalFunctor(molecule, atom, c, m));
			cores.push_back(
					functionT(
							factoryT(world).functor(func).truncate_on_project()));
			func = functorT(
					new CoreOrbitalDerivativeFunctor(molecule, atom, axis, c,
							m));
			dcores.push_back(
					functionT(
							factoryT(world).functor(func).truncate_on_project()));
			bc.push_back(molecule.get_core_bc(atn, c));
		}
	}

	// calc \sum_i occ_i <psi_i|(\sum_c Bc d/dx |core><core|)|psi_i>
	double r = 0.0;
	for (unsigned int c = 0; c < cores.size(); ++c) {
		double rcore = 0.0;
		tensorT rcores = inner(world, cores[c], mo);
		tensorT rdcores = inner(world, dcores[c], mo);
		for (unsigned int i = 0; i < mo.size(); ++i) {
			rcore += rdcores[i] * rcores[i] * occ[i];
		}
		r += 2.0 * bc[c] * rcore;
	}

	return r;
}

void SCF::initial_guess(World & world) {
	START_TIMER(world);
	if (param.restart) {
		load_mos(world);
	} else {
		// Use the initial density and potential to generate a better process map
		functionT rho =
				factoryT(world).functor(
						functorT(
								new MolecularGuessDensityFunctor(molecule,
										aobasis))).truncate_on_project();
		END_TIMER(world, "guess density");
		double nel = rho.trace();
		if (world.rank() == 0)
			print("guess dens trace", nel);

		if (world.size() > 1) {
			TAU_START("guess loadbal");
			START_TIMER(world);
			LoadBalanceDeux < 3 > lb(world);
			real_function_3d vnuc = potentialmanager->vnuclear();
			lb.add_tree(vnuc,
					lbcost<double, 3>(vnucextra * 1.0, vnucextra * 8.0), false);
			lb.add_tree(rho, lbcost<double, 3>(1.0, 8.0), true);

			FunctionDefaults < 3 > ::redistribute(world, lb.load_balance(6.0));
			END_TIMER(world, "guess loadbal");
			TAU_STOP("guess loadbal");
		}

		// Diag approximate fock matrix to get initial mos
		functionT vlocal;
		if (param.nalpha + param.nbeta > 1) {
			TAU_START("guess Coulomb potn");
			START_TIMER(world);
			real_function_3d vnuc = potentialmanager->vnuclear();
			vlocal = vnuc + apply(*coulop, rho);
			END_TIMER(world, "guess Coulomb potn");
			TAU_STOP("guess Coulomb potn");
			bool save = param.spin_restricted;
			param.spin_restricted = true;
			vlocal = vlocal + make_lda_potential(world, rho);
			vlocal.truncate();
			param.spin_restricted = save;
		} else {
			real_function_3d vnuc = potentialmanager->vnuclear();
			vlocal = vnuc;
		}
		rho.clear();
		vlocal.reconstruct();
		if (world.size() > 1) {
			LoadBalanceDeux < 3 > lb(world);
			real_function_3d vnuc = potentialmanager->vnuclear();
			lb.add_tree(vnuc,
					lbcost<double, 3>(vnucextra * 1.0, vnucextra * 8.0), false);
			for (unsigned int i = 0; i < ao.size(); ++i) {
				lb.add_tree(ao[i], lbcost<double, 3>(1.0, 8.0), false);
			}

			FunctionDefaults < 3 > ::redistribute(world, lb.load_balance(6.0));
		}

		tensorT overlap = matrix_inner(world, ao, ao, true);
		TAU_START("guess Kinet potn");
		START_TIMER(world);
		tensorT kinetic = kinetic_energy_matrix(world, ao);
		END_TIMER(world, "guess Kinet potn");
		TAU_STOP("guess Kinet potn");
		reconstruct(world, ao);
		vlocal.reconstruct();
		vecfuncT vpsi = mul_sparse(world, vlocal, ao, vtol);
		compress(world, vpsi);
		truncate(world, vpsi);
		compress(world, ao);
		tensorT potential = matrix_inner(world, vpsi, ao, true);
		vpsi.clear();
		tensorT fock = kinetic + potential;
		fock = 0.5 * (fock + transpose(fock));
		tensorT c, e;

		TAU_START("guess eigen sol");
		START_TIMER(world);
		sygvp(world, fock, overlap, 1, c, e);
		END_TIMER(world, "guess eigen sol");
		TAU_STOP("guess eigen sol");
		print_meminfo(world.rank(), "guess eigen sol");

		// NAR 7/5/2013
		// commented out because it generated a lot of output
		// if(world.rank() == 0 && 0){
		//   print("initial eigenvalues");
		//   print(e);
		//   print("\n\nWSTHORNTON: initial eigenvectors");
		//   print(c);
		// }

		compress(world, ao);

		unsigned int ncore = 0;
		if (param.core_type != "") {
			ncore = molecule.n_core_orb_all();
		}
		amo = transform(world, ao,
				c(_, Slice(ncore, ncore + param.nmo_alpha - 1)), 0.0, true);
		truncate(world, amo);
		normalize(world, amo);
		aeps = e(Slice(ncore, ncore + param.nmo_alpha - 1));

		aocc = tensorT(param.nmo_alpha);
		for (int i = 0; i < param.nalpha; ++i)
			aocc[i] = 1.0;

		aset = std::vector<int>(param.nmo_alpha, 0);
		if (world.rank() == 0)
			std::cout << "alpha set " << 0 << " " << 0 << "-";

		for (int i = 1; i < param.nmo_alpha; ++i) {
			aset[i] = aset[i - 1];
			//vamastd::cout << "aeps -" << i << "- " << aeps[i] << std::endl;
			if (aeps[i] - aeps[i - 1] > 1.5 || aocc[i] != 1.0) {
				++(aset[i]);
				if (world.rank() == 0) {
					std::cout << i - 1 << std::endl;
					std::cout << "alpha set " << aset[i] << " " << i << "-";
				}
			}
		}
		if (world.rank() == 0)
			std::cout << param.nmo_alpha - 1 << std::endl;

		if (param.nbeta && !param.spin_restricted) {
			bmo = transform(world, ao,
					c(_, Slice(ncore, ncore + param.nmo_beta - 1)), 0.0, true);
			truncate(world, bmo);
			normalize(world, bmo);
			beps = e(Slice(ncore, ncore + param.nmo_beta - 1));
			bocc = tensorT(param.nmo_beta);
			for (int i = 0; i < param.nbeta; ++i)
				bocc[i] = 1.0;

			bset = std::vector<int>(param.nmo_beta, 0);
			if (world.rank() == 0)
				std::cout << " beta set " << 0 << " " << 0 << "-";

			for (int i = 1; i < param.nmo_beta; ++i) {
				bset[i] = bset[i - 1];
				if (beps[i] - beps[i - 1] > 1.5 || bocc[i] != 1.0) {
					++(bset[i]);
					if (world.rank() == 0) {
						std::cout << i - 1 << std::endl;
						std::cout << " beta set " << bset[i] << " " << i << "-";
					}
				}
			}
			if (world.rank() == 0)
				std::cout << param.nmo_beta - 1 << std::endl;
		}
	}
}

void SCF::initial_load_bal(World & world) {
	LoadBalanceDeux < 3 > lb(world);
	real_function_3d vnuc = potentialmanager->vnuclear();
	lb.add_tree(vnuc, lbcost<double, 3>(vnucextra * 1.0, vnucextra * 8.0));

	FunctionDefaults < 3 > ::redistribute(world, lb.load_balance(6.0));
}

functionT SCF::make_density(World & world, const tensorT & occ,
		const vecfuncT & v) const {
	vecfuncT vsq = square(world, v);
	compress(world, vsq);
	functionT rho = factoryT(world);
	rho.compress();
	for (unsigned int i = 0; i < vsq.size(); ++i) {
		if (occ[i])
			rho.gaxpy(1.0, vsq[i], occ[i], false);

	}
	world.gop.fence();
	vsq.clear();
	return rho;
}

functionT SCF::make_density(World & world, const tensorT & occ,
		const cvecfuncT & v) {
	reconstruct(world, v); // For max parallelism
	std::vector < functionT > vsq(v.size());
	for (unsigned int i = 0; i < v.size(); i++) {
		vsq[i] = abssq(v[i], false);
	}
	world.gop.fence();

	compress(world, vsq); // since will be using gaxpy for accumulation
	functionT rho = factoryT(world);
	rho.compress();

	for (unsigned int i = 0; i < vsq.size(); ++i) {
		if (occ[i])
			rho.gaxpy(1.0, vsq[i], occ[i], false);

	}
	world.gop.fence();
	vsq.clear();
	rho.truncate();

	return rho;
}

std::vector<poperatorT> SCF::make_bsh_operators(World& world, const tensorT& evals) {
	int nmo = evals.dim(0);
	std::vector < poperatorT > ops(nmo);
	double tol = FunctionDefaults < 3 > ::get_thresh();
	for (int i = 0; i < nmo; ++i) {
		double eps = evals(i);
		if (eps > 0) {
			if (world.rank() == 0) {
				print("bsh: warning: positive eigenvalue", i, eps);
			}
			eps = -0.1;
		}

		ops[i] = poperatorT(
				BSHOperatorPtr3D(world, sqrt(-2.0 * eps), param.lo, tol));
	}

	return ops;
}

/// apply the HF exchange on a set of orbitals

/// @param[in]  world   the world
/// @param[in]  occ     occupation numbers
/// @param[in]  psi     the orbitals in the exchange operator
/// @param[in]  f       the orbitals |i> that the operator is applied on
/// @return     a vector of orbitals  K| i>
vecfuncT SCF::apply_hf_exchange(World & world, const tensorT & occ,
		const vecfuncT & psi, const vecfuncT & f) const {
	const bool same = (&psi == &f);
	int nocc = psi.size();
	int nf = f.size();
	double tol = FunctionDefaults < 3 > ::get_thresh(); /// Important this is consistent with Coulomb
	vecfuncT Kf = zero_functions<double, 3>(world, nf);
	compress(world, Kf);
	reconstruct(world, psi);
	norm_tree(world, psi);
	if (!same) {
		reconstruct(world, f);
		norm_tree(world, f);
	}

	//         // Smaller memory algorithm ... possible 2x saving using i-j sym
	//         for(int i=0; i<nocc; ++i){
	//             if(occ[i] > 0.0){
	//                 vecfuncT psif = mul_sparse(world, psi[i], f, tol); /// was vtol
	//                 truncate(world, psif);
	//                 psif = apply(world, *coulop, psif);
	//                 truncate(world, psif);
	//                 psif = mul_sparse(world, psi[i], psif, tol); /// was vtol
	//                 gaxpy(world, 1.0, Kf, occ[i], psif);
	//             }
	//         }

	// Larger memory algorithm ... use i-j sym if psi==f
	vecfuncT psif;
	for (int i = 0; i < nocc; ++i) {
		int jtop = nf;
		if (same)
			jtop = i + 1;
		for (int j = 0; j < jtop; ++j) {
			psif.push_back(mul_sparse(psi[i], f[j], tol, false));
		}
	}

	world.gop.fence();
	truncate(world, psif);
	psif = apply(world, *coulop, psif);
	truncate(world, psif, tol);
	reconstruct(world, psif);
	norm_tree(world, psif);
	vecfuncT psipsif = zero_functions<double, 3>(world, nf * nocc);
	int ij = 0;
	for (int i = 0; i < nocc; ++i) {
		int jtop = nf;
		if (same)
			jtop = i + 1;
		for (int j = 0; j < jtop; ++j, ++ij) {
			psipsif[i * nf + j] = mul_sparse(psif[ij], psi[i], false);
			if (same && i != j) {
				psipsif[j * nf + i] = mul_sparse(psif[ij], psi[j], false);
			}
		}
	}
	world.gop.fence();
	psif.clear();
	world.gop.fence();
	compress(world, psipsif);
	for (int i = 0; i < nocc; ++i) {
		for (int j = 0; j < nf; ++j) {
			Kf[j].gaxpy(1.0, psipsif[i * nf + j], occ[i], false);
		}
	}
	world.gop.fence();
	psipsif.clear();
	world.gop.fence();

	truncate(world, Kf, tol);
	return Kf;
}

// Used only for initial guess that is always spin-restricted LDA
functionT SCF::make_lda_potential(World & world, const functionT & arho) {
	functionT vlda = copy(arho);
	vlda.reconstruct();
	vlda.unaryop(xc_lda_potential());
	return vlda;
}

vecfuncT SCF::apply_potential(World & world, const tensorT & occ,
		const vecfuncT & amo, const vecfuncT& vf, const vecfuncT& delrho,
		const functionT & vlocal, double & exc, int ispin) {
	functionT vloc = vlocal;
	exc = 0.0;

	//print("DFT", xc.is_dft(), "LDA", xc.is_lda(), "GGA", xc.is_gga(), "POLAR", xc.is_spin_polarized());
	if (xc.is_dft() && !(xc.hf_exchange_coefficient() == 1.0)) {

		if (ispin == 0)
			exc = make_dft_energy(world, vf, ispin);

		TAU_START("DFT potential");
		START_TIMER(world);

		// V_rho
		vloc = vloc + make_dft_potential(world, vf, ispin, 0);

#ifdef MADNESS_HAS_LIBXC
		//
		// What = 0 : Vrho
		// What = 1 : Vsigma_ss
		// What = 2 : Vsigma_ab
		//
		// close shell
		//       v_xc = vrho - Div( 2Vsig_aa * Grad(rho_a))
		// open shell
		//       v_xc = vrho - Div( 2*Vsig_aa*Grad(rho)+ Vsig_ab*Grad(rho_b) + Vsig_ba*Grad(rho_a) + 2*Vsig_bb*Grad(rho_b))
		//

		if (xc.is_gga() ) {
			// get Vsigma_aa (if it is the case and Vsigma_bb)
			functionT vsigaa = make_dft_potential(world, vf, ispin, 1).truncate();
			functionT vsigab;
			if (xc.is_spin_polarized())// V_ab
			vsigab = make_dft_potential(world, vf, ispin, 2).truncate();

			for (int axis=0; axis<3; axis++) {
				functionT gradn = delrho[axis + 3*ispin];
				functionT ddel = vsigaa*gradn;
				ddel.scale(2.0);
				if (xc.is_spin_polarized()) {
					functionT vsab = vsigab*delrho[axis + 3*(1-ispin)];
					ddel = ddel + vsab;
				}
				Derivative<double,3> D = free_space_derivative<double,3>(world, axis);
				functionT vxc2=D(ddel);
				vloc = vloc - vxc2.truncate();
			}
		} //is gga
#endif
		END_TIMER(world, "DFT potential");
		TAU_STOP("DFT potential");
	}

	TAU_START("V*psi");
	START_TIMER(world);
	vecfuncT Vpsi = mul_sparse(world, vloc, amo, vtol);
	END_TIMER(world, "V*psi");
	TAU_STOP("V*psi");
	print_meminfo(world.rank(), "V*psi");
	if (xc.hf_exchange_coefficient()) {
		TAU_START("HF exchange");
		START_TIMER(world);
		vecfuncT Kamo = apply_hf_exchange(world, occ, amo, amo);
		tensorT excv = inner(world, Kamo, amo);
		double exchf = 0.0;
		for (unsigned long i = 0; i < amo.size(); ++i) {
			exchf -= 0.5 * excv[i] * occ[i];
		}
		if (!xc.is_spin_polarized())
			exchf *= 2.0;
		gaxpy(world, 1.0, Vpsi, -xc.hf_exchange_coefficient(), Kamo);
		Kamo.clear();
		END_TIMER(world, "HF exchange");
		TAU_STOP("HF exchange");
		exc = exchf * xc.hf_exchange_coefficient() + exc;
	}
	potentialmanager->apply_nonlocal_potential(world, amo, Vpsi);

	if (param.core_type.substr(0, 3) == "mcp") {
		TAU_START("MCP Core Projector");
		START_TIMER(world);
		gaxpy(world, 1.0, Vpsi, 1.0, core_projection(world, amo));
		END_TIMER(world, "MCP Core Projector");
		TAU_STOP("MCP Core Projector");
	}

	TAU_START("Truncate Vpsi");
	START_TIMER(world);
	truncate(world, Vpsi);
	END_TIMER(world, "Truncate Vpsi");
	TAU_STOP("Truncate Vpsi");
	print_meminfo(world.rank(), "Truncate Vpsi");
	world.gop.fence();
	return Vpsi;
}

tensorT SCF::derivatives(World & world) {
	TAU_START("derivatives");
	START_TIMER(world);

	functionT rho = make_density(world, aocc, amo);
	functionT brho = rho;
	if (!param.spin_restricted)
		brho = make_density(world, bocc, bmo);
	rho.gaxpy(1.0, brho, 1.0);

	vecfuncT dv(molecule.natom() * 3);
	vecfuncT du = zero_functions<double, 3>(world, molecule.natom() * 3);
	tensorT rc(molecule.natom() * 3);
	for (int atom = 0; atom < molecule.natom(); ++atom) {
		for (int axis = 0; axis < 3; ++axis) {
			functorT func(new MolecularDerivativeFunctor(molecule, atom, axis));
			dv[atom * 3 + axis] =
					functionT(
							factoryT(world).functor(func).nofence().truncate_on_project());
			if (param.core_type != ""
					&& molecule.is_potential_defined_atom(atom)) {
				// core potential contribution
				func = functorT(
						new CorePotentialDerivativeFunctor(molecule, atom,
								axis));
				du[atom * 3 + axis] = functionT(
						factoryT(world).functor(func).truncate_on_project());

				// core projector contribution
				rc[atom * 3 + axis] =
						potentialmanager->core_projector_derivative(world, amo,
								aocc, atom, axis);
				if (!param.spin_restricted) {
					if (param.nbeta)
						rc[atom * 3 + axis] +=
								potentialmanager->core_projector_derivative(
										world, bmo, bocc, atom, axis);
				} else {
					rc[atom * 3 + axis] *= 2 * 2;
					// because of 2 electrons in each valence orbital bra+ket
				}
			}
		}
	}

	world.gop.fence();
	tensorT r = inner(world, rho, dv);
	world.gop.fence();
	tensorT ru = inner(world, rho, du);
	dv.clear();
	du.clear();
	world.gop.fence();
	tensorT ra(r.size());
	for (int atom = 0; atom < molecule.natom(); ++atom) {
		for (int axis = 0; axis < 3; ++axis) {
			ra[atom * 3 + axis] = molecule.nuclear_repulsion_derivative(atom,
					axis);
		}
	}
	//if (world.rank() == 0) print("derivatives:\n", r, ru, rc, ra);
	r += ra + ru + rc;
	END_TIMER(world, "derivatives");
	TAU_STOP("derivatives");

	if (world.rank() == 0) {
		print("\n Derivatives (a.u.)\n -----------\n");
		print(
				"  atom        x            y            z          dE/dx        dE/dy        dE/dz");
		print(
				" ------ ------------ ------------ ------------ ------------ ------------ ------------");
		for (int i = 0; i < molecule.natom(); ++i) {
			const Atom& atom = molecule.get_atom(i);
			printf(" %5d %12.6f %12.6f %12.6f %12.6f %12.6f %12.6f\n", i,
					atom.x, atom.y, atom.z, r[i * 3 + 0], r[i * 3 + 1],
					r[i * 3 + 2]);
		}
	}
	return r;
}

tensorT SCF::dipole(World & world) {
	TAU_START("dipole");
	START_TIMER(world);
	tensorT mu(3);
	for (unsigned int axis = 0; axis < 3; ++axis) {
		std::vector<int> x(3, 0);
		x[axis] = true;
		functionT dipolefunc = factoryT(world).functor(
				functorT(new MomentFunctor(x)));
		functionT rho = make_density(world, aocc, amo);
		if (!param.spin_restricted) {
			if (param.nbeta)
				rho += make_density(world, bocc, bmo);
		} else {
			rho.scale(2.0);
		}
		mu[axis] = -dipolefunc.inner(rho);
		mu[axis] += molecule.nuclear_dipole(axis);
	}

	if (world.rank() == 0) {
		print("\n Dipole Moment (a.u.)\n -----------\n");
		print("     x: ", mu[0]);
		print("     y: ", mu[1]);
		print("     z: ", mu[2]);
		print(" Total Dipole Moment: ", mu.normf());
	}
	END_TIMER(world, "dipole");
	TAU_STOP("dipole");

	return mu;
}

void SCF::vector_stats(const std::vector<double> & v, double & rms,
		double & maxabsval) const {
	rms = 0.0;
	maxabsval = v[0];
	for (unsigned int i = 0; i < v.size(); ++i) {
		rms += v[i] * v[i];
		maxabsval = std::max<double>(maxabsval, std::abs(v[i]));
	}
	rms = sqrt(rms / v.size());
}

vecfuncT SCF::compute_residual(World & world, tensorT & occ, tensorT & fock,
		const vecfuncT & psi, vecfuncT & Vpsi, double & err) {
	double trantol = vtol / std::min(30.0, double(psi.size()));
	int nmo = psi.size();

	tensorT eps(nmo);
	for (int i = 0; i < nmo; ++i) {
		eps(i) = std::min(-0.05, fock(i, i));
		fock(i, i) -= eps(i);
	}
	vecfuncT fpsi = transform(world, psi, fock, trantol, true);

	for (int i = 0; i < nmo; ++i) { // Undo the damage
		fock(i, i) += eps(i);
	}

	gaxpy(world, 1.0, Vpsi, -1.0, fpsi);
	fpsi.clear();
	std::vector<double> fac(nmo, -2.0);
	scale(world, Vpsi, fac);
	std::vector < poperatorT > ops = make_bsh_operators(world, eps);
	set_thresh(world, Vpsi, FunctionDefaults < 3 > ::get_thresh());
	if (world.rank() == 0)
		std::cout << "entering apply\n";

	TAU_START("Apply BSH");
	START_TIMER(world);
	vecfuncT new_psi = apply(world, ops, Vpsi);
	END_TIMER(world, "Apply BSH");
	TAU_STOP("Apply BSH");
	ops.clear();
	Vpsi.clear();
	world.gop.fence();

	// Thought it was a bad idea to truncate *before* computing the residual
	// but simple tests suggest otherwise ... no more iterations and
	// reduced iteration time from truncating.
	TAU_START("Truncate new psi");
	START_TIMER(world);
	truncate(world, new_psi);
	END_TIMER(world, "Truncate new psi");
	TAU_STOP("Truncate new psi");

	vecfuncT r = sub(world, psi, new_psi);
	std::vector<double> rnorm = norm2s(world, r);
	if (world.rank() == 0)
		print("residuals", rnorm);
	double rms, maxval;
	vector_stats(rnorm, rms, maxval);
	err = maxval;
	if (world.rank() == 0)
		print("BSH residual: rms", rms, "   max", maxval);

	return r;
}

tensorT SCF::make_fock_matrix(World & world, const vecfuncT & psi,
		const vecfuncT & Vpsi, const tensorT & occ, double & ekinetic) const {
	TAU_START("PE matrix");
	START_TIMER(world);
	tensorT pe = matrix_inner(world, Vpsi, psi, true);
	END_TIMER(world, "PE matrix");
	TAU_STOP("PE matrix");
	TAU_START("KE matrix");
	START_TIMER(world);
	tensorT ke = kinetic_energy_matrix(world, psi);
	END_TIMER(world, "KE matrix");
	TAU_STOP("KE matrix");
	TAU_START("Make fock matrix rest");
	START_TIMER(world);
	int nocc = occ.size();
	ekinetic = 0.0;
	for (int i = 0; i < nocc; ++i) {
		ekinetic += occ[i] * ke(i, i);
	}
	ke += pe;
	pe = tensorT();
	ke.gaxpy(0.5, transpose(ke), 0.5);
	END_TIMER(world, "Make fock matrix rest");
	TAU_STOP("Make fock matrix rest");
	return ke;
}

/// Compute the two-electron integrals over the provided set of orbitals

/// Returned is a *replicated* tensor of \f$(ij|kl)\f$ with \f$i>=j\f$
/// and \f$k>=l\f$.  The symmetry \f$(ij|kl)=(kl|ij)\f$ is enforced.
Tensor<double> SCF::twoint(World& world, const vecfuncT& psi) const {
	double tol = FunctionDefaults < 3 > ::get_thresh(); /// Important this is consistent with Coulomb
	reconstruct(world, psi);
	norm_tree(world, psi);

	// Efficient version would use mul_sparse vector interface
	vecfuncT pairs;
	for (unsigned int i = 0; i < psi.size(); ++i) {
		for (unsigned int j = 0; j <= i; ++j) {
			pairs.push_back(mul_sparse(psi[i], psi[j], tol, false));
		}
	}

	world.gop.fence();
	truncate(world, pairs);
	vecfuncT Vpairs = apply(world, *coulop, pairs);

	return matrix_inner(world, pairs, Vpairs, true);
}

tensorT SCF::matrix_exponential(const tensorT& A) const {
	const double tol = 1e-13;
	MADNESS_ASSERT(A.dim((0) == A.dim(1)));

	// Scale A by a power of 2 until it is "small"
	double anorm = A.normf();
	int n = 0;
	double scale = 1.0;
	while (anorm * scale > 0.1) {
		++n;
		scale *= 0.5;
	}
	tensorT B = scale * A;    // B = A*2^-n

	// Compute exp(B) using Taylor series
	tensorT expB = tensorT(2, B.dims());
	for (int i = 0; i < expB.dim(0); ++i)
		expB(i, i) = 1.0;

	int k = 1;
	tensorT term = B;
	while (term.normf() > tol) {
		expB += term;
		term = inner(term, B);
		++k;
		term.scale(1.0 / k);
	}

	// Repeatedly square to recover exp(A)
	while (n--) {
		expB = inner(expB, expB);
	}

	return expB;
}

/// compute the unitary transformation that diagonalizes the fock matrix

/// @param[in]	world	the world
/// @param[in]	overlap	the overlap matrix of the orbitals
/// @param[inout]	fock	the fock matrix; diagonal upon exit
/// @param[out]	evals	the orbital energies
/// @param[in]	occ	the occupation numbers
/// @param[in]	thresh_degenerate	threshold for orbitals being degenerate
/// @return		the unitary matrix U: U^T F U = evals
tensorT SCF::get_fock_transformation(World& world, const tensorT& overlap,
		tensorT& fock, tensorT& evals, const tensorT& occ,
		const double thresh_degenerate) const {

	TAU_START("Diagonalization Fock-mat w sygv");
	START_TIMER(world);
	tensorT U;
	sygvp(world, fock, overlap, 1, U, evals);
	END_TIMER(world, "Diagonalization Fock-mat w sygv");
	TAU_STOP("Diagonalization Fock-mat w sygv");

	long nmo = fock.dim(0);

	TAU_START("Diagonalization rest");
	START_TIMER(world);
	// Within blocks with the same occupation number attempt to
	// keep orbitals in the same order (to avoid confusing the
	// non-linear solver).
	// !!!!!!!!!!!!!!!!! NEED TO RESTRICT TO OCCUPIED STATES?
	bool switched = true;
	while (switched) {
		switched = false;
		for (int i = 0; i < nmo; i++) {
			for (int j = i + 1; j < nmo; j++) {
				if (occ(i) == occ(j)) {
					double sold = U(i, i) * U(i, i) + U(j, j) * U(j, j);
					double snew = U(i, j) * U(i, j) + U(j, i) * U(j, i);
					if (snew > sold) {
						tensorT tmp = copy(U(_, i));
						U(_, i) = U(_, j);
						U(_, j) = tmp;
						std::swap(evals[i], evals[j]);
						switched = true;
					}
				}
			}
		}
	}
	// Fix phases.
	for (long i = 0; i < nmo; ++i)
		if (U(i, i) < 0.0)
			U(_, i).scale(-1.0);

	// Rotations between effectively degenerate states confound
	// the non-linear equation solver ... undo these rotations
	long ilo = 0; // first element of cluster
	while (ilo < nmo - 1) {
		long ihi = ilo;
		while (fabs(evals[ilo] - evals[ihi + 1])
				< thresh_degenerate * 10.0 * std::max(fabs(evals[ilo]), 1.0)) {
			++ihi;
			if (ihi == nmo - 1)
				break;
		}
		long nclus = ihi - ilo + 1;
		if (nclus > 1) {
			//print("   found cluster", ilo, ihi);
			tensorT q = copy(U(Slice(ilo, ihi), Slice(ilo, ihi)));
			//print(q);
			// Special code just for nclus=2
			// double c = 0.5*(q(0,0) + q(1,1));
			// double s = 0.5*(q(0,1) - q(1,0));
			// double r = sqrt(c*c + s*s);
			// c /= r;
			// s /= r;
			// q(0,0) = q(1,1) = c;
			// q(0,1) = -s;
			// q(1,0) = s;

			// Iteratively construct unitary rotation by
			// exponentiating the antisymmetric part of the matrix
			// ... is quadratically convergent so just do 3
			// iterations
			tensorT rot = matrix_exponential(-0.5 * (q - transpose(q)));
			q = inner(q, rot);
			tensorT rot2 = matrix_exponential(-0.5 * (q - transpose(q)));
			q = inner(q, rot2);
			tensorT rot3 = matrix_exponential(-0.5 * (q - transpose(q)));
			q = inner(rot, inner(rot2, rot3));
			U(_, Slice(ilo, ihi)) = inner(U(_, Slice(ilo, ihi)), q);
		}
		ilo = ihi + 1;
	}

	world.gop.broadcast(U.ptr(), U.size(), 0);
	world.gop.broadcast(evals.ptr(), evals.size(), 0);

	fock = 0;
	for (unsigned int i = 0; i < nmo; ++i)
		fock(i, i) = evals(i);
	return U;
}

/// diagonalize the fock matrix, taking care of degenerate states

/// Vpsi is passed in to make sure orbitals and Vpsi are in phase
/// @param[in]	world	the world
/// @param[inout]	fock	the fock matrix (diagonal upon exit)
/// @param[inout]	psi		the orbitals
/// @param[inout]	Vpsi	the orbital times the potential
/// @param[out]	evals	the orbital energies
/// @param[in]	occ		occupation numbers
/// @param[in]	thresh	threshold for rotation and truncation
/// @return		the unitary matrix U: U^T F U = evals
tensorT SCF::diag_fock_matrix(World& world, tensorT& fock, vecfuncT& psi,
		vecfuncT& Vpsi, tensorT& evals, const tensorT& occ,
		const double thresh) const {

	// compute the unitary transformation matrix U that diagonalizes
	// the fock matrix
	tensorT overlap = matrix_inner(world, psi, psi, true);
	tensorT U = get_fock_transformation(world, overlap, fock, evals, occ,
			thresh);

	// transform the orbitals and the orbitals times the potential
	Vpsi = transform(world, Vpsi, U, vtol / std::min(30.0, double(psi.size())),
			false);
	psi = transform(world, psi, U,
			FunctionDefaults < 3
					> ::get_thresh() / std::min(30.0, double(psi.size())),
			true);
	truncate(world, Vpsi, vtol, false);
	truncate(world, psi);
	normalize(world, psi);

	END_TIMER(world, "Diagonalization rest");
	TAU_STOP("Diagonalization rest");
	return U;
}

void SCF::loadbal(World & world, functionT & arho, functionT & brho,
		functionT & arho_old, functionT & brho_old, subspaceT & subspace) {
	if (world.size() == 1)
		return;

	LoadBalanceDeux < 3 > lb(world);
	real_function_3d vnuc = potentialmanager->vnuclear();
	lb.add_tree(vnuc, lbcost<double, 3>(vnucextra * 1.0, vnucextra * 8.0),
			false);
	lb.add_tree(arho, lbcost<double, 3>(1.0, 8.0), false);
	for (unsigned int i = 0; i < amo.size(); ++i) {
		lb.add_tree(amo[i], lbcost<double, 3>(1.0, 8.0), false);
	}
	if (param.nbeta && !param.spin_restricted) {
		lb.add_tree(brho, lbcost<double, 3>(1.0, 8.0), false);
		for (unsigned int i = 0; i < bmo.size(); ++i) {
			lb.add_tree(bmo[i], lbcost<double, 3>(1.0, 8.0), false);
		}
	}
	world.gop.fence();

	FunctionDefaults < 3 > ::redistribute(world, lb.load_balance(6.0)); // 6.0 needs retuning after vnucextra
}

void SCF::rotate_subspace(World& world, const tensorT& U, subspaceT& subspace,
		int lo, int nfunc, double trantol) const {
	for (unsigned int iter = 0; iter < subspace.size(); ++iter) {
		vecfuncT& v = subspace[iter].first;
		vecfuncT& r = subspace[iter].second;
		transform(world, vecfuncT(&v[lo], &v[lo + nfunc]), U, trantol, false);
		transform(world, vecfuncT(&r[lo], &r[lo + nfunc]), U, trantol, true);
	}
}

void SCF::update_subspace(World & world, vecfuncT & Vpsia, vecfuncT & Vpsib,
		tensorT & focka, tensorT & fockb, subspaceT & subspace, tensorT & Q,
		double & bsh_residual, double & update_residual) {
	double aerr = 0.0, berr = 0.0;
	vecfuncT vm = amo;

	// Orbitals with occ!=1.0 exactly must be solved for as eigenfunctions
	// so zero out off diagonal lagrange multipliers
	for (int i = 0; i < param.nmo_alpha; i++) {
		if (aocc[i] != 1.0) {
			double tmp = focka(i, i);
			focka(i, _) = 0.0;
			focka(_, i) = 0.0;
			focka(i, i) = tmp;
		}
	}

	vecfuncT rm = compute_residual(world, aocc, focka, amo, Vpsia, aerr);
	if (param.nbeta != 0 && !param.spin_restricted) {
		for (int i = 0; i < param.nmo_beta; i++) {
			if (bocc[i] != 1.0) {
				double tmp = fockb(i, i);
				fockb(i, _) = 0.0;
				fockb(_, i) = 0.0;
				fockb(i, i) = tmp;
			}
		}

		vecfuncT br = compute_residual(world, bocc, fockb, bmo, Vpsib, berr);
		vm.insert(vm.end(), bmo.begin(), bmo.end());
		rm.insert(rm.end(), br.begin(), br.end());
	}
	bsh_residual = std::max(aerr, berr);
	world.gop.broadcast(bsh_residual, 0);
	compress(world, vm, false);
	compress(world, rm, false);
	world.gop.fence();
	subspace.push_back(pairvecfuncT(vm, rm));
	int m = subspace.size();
	tensorT ms(m);
	tensorT sm(m);
	for (int s = 0; s < m; ++s) {
		const vecfuncT & vs = subspace[s].first;
		const vecfuncT & rs = subspace[s].second;
		for (unsigned int i = 0; i < vm.size(); ++i) {
			ms[s] += vm[i].inner_local(rs[i]);
			sm[s] += vs[i].inner_local(rm[i]);
		}
	}

	world.gop.sum(ms.ptr(), m);
	world.gop.sum(sm.ptr(), m);
	tensorT newQ(m, m);
	if (m > 1)
		newQ(Slice(0, -2), Slice(0, -2)) = Q;

	newQ(m - 1, _) = ms;
	newQ(_, m - 1) = sm;
	Q = newQ;
	//if (world.rank() == 0) { print("kain Q"); print(Q); }
	tensorT c;
	if (world.rank() == 0) {
		double rcond = 1e-12;
		while (1) {
			c = KAIN(Q, rcond);
			//if (world.rank() == 0) print("kain c:", c);
			if (std::abs(c[m - 1]) < 3.0) {
				break;
			} else if (rcond < 0.01) {
				print("Increasing subspace singular value threshold ", c[m - 1],
						rcond);
				rcond *= 100;
			} else {
				print("Forcing full step due to subspace malfunction");
				c = 0.0;
				c[m - 1] = 1.0;
				break;
			}
		}
	}

	world.gop.broadcast_serializable(c, 0);
	if (world.rank() == 0) {
		print("Subspace solution", c);
	}
	TAU_START("Subspace transform");
	START_TIMER(world);
	vecfuncT amo_new = zero_functions<double, 3>(world, amo.size());
	vecfuncT bmo_new = zero_functions<double, 3>(world, bmo.size());
	compress(world, amo_new, false);
	compress(world, bmo_new, false);
	world.gop.fence();
	for (unsigned int m = 0; m < subspace.size(); ++m) {
		const vecfuncT & vm = subspace[m].first;
		const vecfuncT & rm = subspace[m].second;
		const vecfuncT vma(vm.begin(), vm.begin() + amo.size());
		const vecfuncT rma(rm.begin(), rm.begin() + amo.size());
		const vecfuncT vmb(vm.end() - bmo.size(), vm.end());
		const vecfuncT rmb(rm.end() - bmo.size(), rm.end());
		gaxpy(world, 1.0, amo_new, c(m), vma, false);
		gaxpy(world, 1.0, amo_new, -c(m), rma, false);
		gaxpy(world, 1.0, bmo_new, c(m), vmb, false);
		gaxpy(world, 1.0, bmo_new, -c(m), rmb, false);
	}
	world.gop.fence();
	END_TIMER(world, "Subspace transform");
	TAU_STOP("Subspace transform");
	if (param.maxsub <= 1) {
		subspace.clear();
	} else if (subspace.size() == param.maxsub) {
		subspace.erase(subspace.begin());
		Q = Q(Slice(1, -1), Slice(1, -1));
	}

	do_step_restriction(world, amo, amo_new, "alpha");
	orthonormalize(world, amo_new);
	amo = amo_new;

	if (!param.spin_restricted && param.nbeta != 0) {
		do_step_restriction(world, bmo, bmo_new, "beta");
		orthonormalize(world, bmo_new);
		bmo = bmo_new;
	} else {
		bmo = amo;
	}
}

/// perform step restriction following the KAIN solver

/// undo the rotation from the KAIN solver if the rotation exceeds the
/// maxrotn parameter
/// @param[in]		world	the world
/// @param[in]		mo		vector of orbitals from previous iteration
/// @param[inout]	new_mo	vector of orbitals from the KAIN solver
/// @param[in]		spin	"alpha" or "beta" for user information
/// @return			max residual
double SCF::do_step_restriction(World& world, const vecfuncT& mo, vecfuncT& mo_new,
		std::string spin) const {
	std::vector<double> anorm = norm2s(world, sub(world, mo, mo_new));
	int nres = 0;
	for (unsigned int i = 0; i < mo.size(); ++i) {
		if (anorm[i] > param.maxrotn) {
			double s = param.maxrotn / anorm[i];
			++nres;
			if (world.rank() == 0) {
				if (nres == 1)
					printf("  restricting step for %s orbitals:", spin.c_str());
				printf(" %d", i);
			}
			mo_new[i].gaxpy(s, mo[i], 1.0 - s, false);
		}
	}
	if (nres > 0 && world.rank() == 0)
		printf("\n");

	world.gop.fence();
	double rms, maxval;
	vector_stats(anorm, rms, maxval);
	if (world.rank() == 0)
		print("Norm of vector changes", spin, ": rms", rms, "   max", maxval);
	return maxval;
}

/// orthonormalize the vectors

/// @param[in]		world	the world
/// @param[inout]	amo_new	the vectors to be orthonormalized
void SCF::orthonormalize(World& world, vecfuncT& amo_new) const {
	TAU_START("Orthonormalize");
	START_TIMER(world);
	double trantol = vtol / std::min(30.0, double(amo.size()));
	normalize(world, amo_new);
	amo_new = transform(world, amo_new,
			Q3(matrix_inner(world, amo_new, amo_new)), trantol, true);
	truncate(world, amo_new);
	normalize(world, amo_new);
	END_TIMER(world, "Orthonormalize");
	TAU_STOP("Orthonormalize");
}


void SCF::propagate(World& world, double omega, int step0) {
	// Load molecular orbitals
	set_protocol < 3 > (world, 1e-4);
	make_nuclear_potential(world);
	initial_load_bal(world);
	load_mos(world);

	int nstep = 1000;
	double time_step = 0.05;

	double strength = 0.1;

	// temporary way of doing this for now
//      VextCosFunctor<double> Vext(world,new DipoleFunctor(2),omega);
	functionT fdipx =
			factoryT(world).functor(functorT(new DipoleFunctor(0))).initial_level(
					4);
	functionT fdipy =
			factoryT(world).functor(functorT(new DipoleFunctor(1))).initial_level(
					4);
	functionT fdipz =
			factoryT(world).functor(functorT(new DipoleFunctor(2))).initial_level(
					4);

	world.gop.broadcast(time_step);
	world.gop.broadcast(nstep);

	// Need complex orbitals :(
	double thresh = 1e-4;
	cvecfuncT camo = zero_functions<double_complex, 3>(world, param.nalpha);
	cvecfuncT cbmo = zero_functions<double_complex, 3>(world, param.nbeta);
	for (int iorb = 0; iorb < param.nalpha; iorb++) {
		camo[iorb] = std::exp(double_complex(0.0, 2 * constants::pi * strength))
				* amo[iorb];
		camo[iorb].truncate(thresh);
	}
	if (!param.spin_restricted && param.nbeta) {
		for (int iorb = 0; iorb < param.nbeta; iorb++) {
			cbmo[iorb] = std::exp(
					double_complex(0.0, 2 * constants::pi * strength))
					* bmo[iorb];
			cbmo[iorb].truncate(thresh);
		}
	}

	// Create free particle propagator
	// Have no idea what to set "c" to
	double c = 20.0;
	printf("Creating G\n");
	Convolution1D < double_complex > *G = qm_1d_free_particle_propagator(
			FunctionDefaults < 3 > ::get_k(), c, 0.5 * time_step,
			2.0 * param.L);
	printf("Done creating G\n");

	// Start iteration over time
	for (int step = 0; step < nstep; step++) {
//        if (world.rank() == 0) printf("Iterating step %d:\n\n", step);
		double t = time_step * step;
//        iterate_trotter(world, G, Vext, camo, cbmo, t, time_step);
		iterate_trotter(world, G, camo, cbmo, t, time_step, thresh);
		functionT arho = make_density(world, aocc, camo);
		functionT brho =
				(!param.spin_restricted && param.nbeta) ?
						make_density(world, aocc, camo) : copy(arho);
		functionT rho = arho + brho;
		double xval = inner(fdipx, rho);
		double yval = inner(fdipy, rho);
		double zval = inner(fdipz, rho);
		if (world.rank() == 0)
			printf("%15.7f%15.7f%15.7f%15.7f\n", t, xval, yval, zval);
	}
}

complex_functionT APPLY(const complex_operatorT* q1d,
		const complex_functionT& psi) {
	complex_functionT r = psi; // Shallow copy violates constness !!!!!!!!!!!!!!!!!
	coordT lo, hi;
	lo[2] = -10;
	hi[2] = +10;

	r.reconstruct();
	r.broaden();
	r.broaden();
	r.broaden();
	r.broaden();
	r = apply_1d_realspace_push(*q1d, r, 2);
	r.sum_down();
	r = apply_1d_realspace_push(*q1d, r, 1);
	r.sum_down();
	r = apply_1d_realspace_push(*q1d, r, 0);
	r.sum_down();

	return r;
}

void SCF::iterate_trotter(World& world, Convolution1D<double_complex>* G,
		cvecfuncT& camo, cvecfuncT& cbmo, double t, double time_step,
		double thresh) {

	// first kinetic energy apply
	cvecfuncT camo2 = zero_functions<double_complex, 3>(world, param.nalpha);
	cvecfuncT cbmo2 = zero_functions<double_complex, 3>(world, param.nbeta);
	for (int iorb = 0; iorb < param.nalpha; iorb++) {
//        if (world.rank()) printf("Apply free-particle Green's function to alpha orbital %d\n", iorb);
		camo2[iorb] = APPLY(G, camo[iorb]);
		camo2[iorb].truncate(thresh);
	}
	if (!param.spin_restricted && param.nbeta) {
		for (int iorb = 0; iorb < param.nbeta; iorb++) {
			cbmo2[iorb] = APPLY(G, cbmo[iorb]);
			cbmo2[iorb].truncate(thresh);
		}
	}
	// Construct new density
//      START_TIMER(world);
	functionT arho = make_density(world, aocc, amo), brho;

	if (param.nbeta) {
		if (param.spin_restricted) {
			brho = arho;
		} else {
			brho = make_density(world, bocc, bmo);
		}
	} else {
		brho = functionT(world); // zero
	}
	functionT rho = arho + brho;
//      END_TIMER(world, "Make densities");

	// Do RPA only for now
	real_function_3d vnuc = potentialmanager->vnuclear();
	functionT vlocal = vnuc;
//      START_TIMER(world);
	functionT vcoul = apply(*coulop, rho);
//      END_TIMER(world, "Coulomb");
//      vlocal += vcoul + Vext(t+0.5*time_step);
//      vlocal += vcoul + std::cos(0.1*(t+0.5*time_step))*fdip;

	// exponentiate potential
//      if (world.rank()) printf("Apply Kohn-Sham potential to orbitals\n");
	complex_functionT expV = make_exp(time_step, vlocal);
	cvecfuncT camo3 = mul_sparse(world, expV, camo2, vtol, false);
	world.gop.fence();

	// second kinetic energy apply
	for (int iorb = 0; iorb < param.nalpha; iorb++) {
//        if (world.rank() == 0) printf("Apply free-particle Green's function to alpha orbital %d\n", iorb);
		camo3[iorb].truncate(thresh);
		camo[iorb] = APPLY(G, camo3[iorb]);
		camo[iorb].truncate();
	}
	if (!param.spin_restricted && param.nbeta) {
		cvecfuncT cbmo3 = mul_sparse(world, expV, cbmo2, vtol, false);

		// second kinetic energy apply
		for (int iorb = 0; iorb < param.nbeta; iorb++) {
			cbmo[iorb] = APPLY(G, cbmo3[iorb]);
			cbmo[iorb].truncate();
		}
	}
}

// For given protocol, solve the DFT/HF/response equations
void SCF::solve(World & world) {
	functionT arho_old, brho_old;
	const double dconv = std::max(FunctionDefaults < 3 > ::get_thresh(),
			param.dconv);
	const double trantol = vtol / std::min(30.0, double(amo.size()));
	const double tolloc = 1e-3;
	double update_residual = 0.0, bsh_residual = 0.0;
	subspaceT subspace;
	tensorT Q;
	bool do_this_iter = true;
	// Shrink subspace until stop localizing/canonicalizing
	int maxsub_save = param.maxsub;
	param.maxsub = 2;

	//  if(param.absolvent){ //param.physisorption||param.svpe||
	//     functorT rhon_functor(new NuclearDensityFunctor(molecule));
	//     if (world.rank()==0)
	//         print("Projecting Nuclear Charge Density");
	//     rhon = real_factory_3d(world).functor(rhon_functor).truncate_on_project().truncate_mode(0); // nuclear charge density//Jacob added
	//     rhon.truncate();
	//     // coord_3d lo(0.0), hi(0.0);
	//     //lo[0] = -20.0;
	//     // hi[0] = 20.0;
	//     double total_rhon = rhon.trace();
	//     if(world.rank()==0)
	//         print("Nuclear Charge ", total_rhon);

	// }
	/* //Initial electrostatic potential for DFT solvation moldel
	 if(param.absolvent){
	 rhoT = rhon - vacuo_rho;
	 real_convolution_3d op = madness::CoulombOperator(world, param.lo, param.lo);
	 functionT U_guess = op(rhoT);
	 print("TOTAL CHARGE", rhoT.trace());
	 DFTSolventSolver DFTSsolver(vacuo_rho,rhoT,param.rho_0,param.epsilon_2,param.maxiter,world,param.Gamma,param.beta \
                                            ,std::max(1e-5,1e-7));
	 Uabinit = DFTSsolver.ESP(U_guess);
	 }*/
	for (int iter = 0; iter < param.maxiter; ++iter) {
		if (world.rank() == 0)
			printf("\nIteration %d at time %.1fs\n\n", iter, wall_time());

		if (iter > 0 && update_residual < 0.1) {
			//do_this_iter = false;
			param.maxsub = maxsub_save;
		}

		if (param.localize && do_this_iter) {
			tensorT U;
			if (param.localize_pm) {
				U = localize_PM(world, amo, aset, tolloc, 0.25, iter == 0,
						true);
			} else {
				U = localize_boys(world, amo, aset, tolloc, 0.25, iter == 0);
			}
			amo = transform(world, amo, U, trantol, true);
			truncate(world, amo);
			normalize(world, amo);
			rotate_subspace(world, U, subspace, 0, amo.size(), trantol);
			if (!param.spin_restricted && param.nbeta != 0) {
				if (param.localize_pm) {
					U = localize_PM(world, bmo, bset, tolloc, 0.25, iter == 0,
							true);
				} else {
					U = localize_boys(world, bmo, bset, tolloc, 0.25,
							iter == 0);
				}
				bmo = transform(world, bmo, U, trantol, true);
				truncate(world, bmo);
				normalize(world, bmo);
				rotate_subspace(world, U, subspace, amo.size(), bmo.size(),
						trantol);
			}
		}

		TAU_START("Make densities");
		START_TIMER(world);
		functionT arho = make_density(world, aocc, amo), brho;

		if (param.nbeta) {
			if (param.spin_restricted) {
				brho = arho;
			} else {
				brho = make_density(world, bocc, bmo);
			}
		} else {
			brho = functionT(world); // zero
		}
		END_TIMER(world, "Make densities");
		TAU_STOP("Make densities");
		print_meminfo(world.rank(), "Make densities");

		if (iter < 2 || (iter % 10) == 0) {
			START_TIMER(world);
			loadbal(world, arho, brho, arho_old, brho_old, subspace);
			END_TIMER(world, "Load balancing");
			print_meminfo(world.rank(), "Load balancing");
		}
		double da = 0.0, db = 0.0;
		if (iter > 0) {
			da = (arho - arho_old).norm2();
			db = (brho - brho_old).norm2();
			if (world.rank() == 0)
				print("delta rho", da, db, "residuals", bsh_residual,
						update_residual);

		}

		arho_old = arho;
		brho_old = brho;
		functionT rho = arho + brho;
		//            rho_elec = rho;// to be used in the solvent solver
		//print("Rho Elec Solvent , Gas Phase Density ", rho_elec.trace(), vacuo_rho.trace()); // DEBUG
		// double Xrhoetrace = rho_elec.trace(); //DEBUG
		rho.truncate();
		real_function_3d vnuc = potentialmanager->vnuclear();
		double enuclear = inner(rho, vnuc);

		// DEBUG
		//double rhotrace = rho.trace();
		//double vnuctrace = vnuc.trace();
		// if (world.rank() == 0) printf("DEBUG %.12f %.12f %.12f\n", Xrhoetrace, rhotrace, vnuctrace);
		// END DEBUG

		TAU_START("Coulomb");
		START_TIMER(world);
		functionT vcoul = apply(*coulop, rho);
		functionT vlocal;
		END_TIMER(world, "Coulomb");
		TAU_STOP("Coulomb");
		print_meminfo(world.rank(), "Coulomb");

		double ecoulomb = 0.5 * inner(rho, vcoul);
		rho.clear(false);
		// /*==========================================================================================
		//   Instantiating SVPE and Isodensity solvation models
		//   ============================================================================================*/
		// if (param.absolvent){
		//     //Abinitio Solvation Model
		//     rhoT = rhon - rho_elec;
		//     //  print("DEBUG TOTAL CHARGE", rhoT.trace());
		//     DFTSolventSolver DFTSsolver(vacuo_rho,rhoT,param.rho_0,param.epsilon_2,param.maxiter,world,param.Gamma,param.beta
		//                                 ,std::max(1e-5,1e-6));
		//     Uabinit = DFTSsolver.ESP();
		//     real_functor_3d molecular_mask_functor(new MolecularVolumeMask(param.sigma,molecule.atomic_radii
		//                                                                  ,molecule.get_all_coords_vec()));
		//     //print("DEBUG Interaction Pot", Uabinit.trace());
		//     // mol_mask = real_factory_3d(world).functor(molecular_mask_functor);
		// }
		// if(param.absolvent)
		//     vlocal = vcoul + vnuc + Uabinit;
		// else
		vlocal = vcoul + vnuc;

		vcoul.clear(false);
		vlocal.truncate();
		double exca = 0.0, excb = 0.0;

		vecfuncT vf, delrho;
		if (xc.is_dft()) {
			arho.reconstruct();
			if (param.nbeta != 0 && xc.is_spin_polarized())
				brho.reconstruct();
			// brho.reconstruct();

			vf.push_back(arho);

			if (xc.is_spin_polarized())
				vf.push_back(brho);

			if (xc.is_gga()) {

				for (int axis = 0; axis < 3; ++axis)
					delrho.push_back((*gradop[axis])(arho, false)); // delrho
				if (xc.is_spin_polarized())
					for (int axis = 0; axis < 3; ++axis)
						delrho.push_back((*gradop[axis])(brho, false));

				world.gop.fence(); // NECESSARY

				vf.push_back(
						delrho[0] * delrho[0] + delrho[1] * delrho[1]
								+ delrho[2] * delrho[2]);     // sigma_aa

				if (xc.is_spin_polarized())
					vf.push_back(
							delrho[0] * delrho[3] + delrho[1] * delrho[4]
									+ delrho[2] * delrho[5]); // sigma_ab
				if (xc.is_spin_polarized())
					vf.push_back(
							delrho[3] * delrho[3] + delrho[4] * delrho[4]
									+ delrho[5] * delrho[5]); // sigma_bb

				world.gop.fence(); // NECESSARY
			}
			if (vf.size()) {
				reconstruct(world, vf);
				arho.refine_to_common_level(vf); // Ugly but temporary (I hope!)
			}
		}

		vecfuncT Vpsia = apply_potential(world, aocc, amo, vf, delrho, vlocal,
				exca, 0);
		vecfuncT Vpsib;
		if (!param.spin_restricted && param.nbeta) {
			Vpsib = apply_potential(world, bocc, bmo, vf, delrho, vlocal, excb,
					1);
		}

		double ekina = 0.0, ekinb = 0.0;
		tensorT focka = make_fock_matrix(world, amo, Vpsia, aocc, ekina);
		tensorT fockb = focka;

		if (!param.spin_restricted && param.nbeta != 0)
			fockb = make_fock_matrix(world, bmo, Vpsib, bocc, ekinb);
		else if (param.nbeta != 0) {
			ekinb = ekina;
		}

		if (!param.localize && do_this_iter) {
			tensorT U = diag_fock_matrix(world, focka, amo, Vpsia, aeps, aocc,
					FunctionDefaults < 3 > ::get_thresh());
			rotate_subspace(world, U, subspace, 0, amo.size(), trantol);
			if (!param.spin_restricted && param.nbeta != 0) {
				U = diag_fock_matrix(world, fockb, bmo, Vpsib, beps, bocc,
						FunctionDefaults < 3 > ::get_thresh());
				rotate_subspace(world, U, subspace, amo.size(), bmo.size(),
						trantol);
			}
		}

		double enrep = molecule.nuclear_repulsion_energy();
		double ekinetic = ekina + ekinb;
		double exc = exca + excb;
		double etot = ekinetic + enuclear + ecoulomb + exc + enrep;
		current_energy = etot;
		esol = etot;

		if (world.rank() == 0) {
			printf("\n              kinetic %16.8f\n", ekinetic);
			printf("   nuclear attraction %16.8f\n", enuclear);
			printf("              coulomb %16.8f\n", ecoulomb);
			printf(" exchange-correlation %16.8f\n", exc);
			printf("    nuclear-repulsion %16.8f\n", enrep);
			printf("                total %16.8f\n\n", etot);
		}

		if (iter > 0) {
			//print("##convergence criteria: density delta=", da < dconv * molecule.natom() && db < dconv * molecule.natom(), ", bsh_residual=", (param.conv_only_dens || bsh_residual < 5.0*dconv));
			if (da < dconv * molecule.natom() && db < dconv * molecule.natom()
					&& (param.conv_only_dens || bsh_residual < 5.0 * dconv)) {
				if (world.rank() == 0) {
					print("\nConverged!\n");
				}

				// Diagonalize to get the eigenvalues and if desired the final eigenvectors
				tensorT U;
				tensorT overlap = matrix_inner(world, amo, amo, true);

				TAU_START("focka eigen sol");
				START_TIMER(world);
				sygvp(world, focka, overlap, 1, U, aeps);
				END_TIMER(world, "focka eigen sol");
				TAU_STOP("focka eigen sol");

				if (!param.localize) {
					amo = transform(world, amo, U, trantol, true);
					truncate(world, amo);
					normalize(world, amo);
				}
				if (param.nbeta != 0 && !param.spin_restricted) {
					overlap = matrix_inner(world, bmo, bmo, true);

					TAU_START("fockb eigen sol");
					START_TIMER(world);
					sygvp(world, fockb, overlap, 1, U, beps);
					END_TIMER(world, "fockb eigen sol");
					TAU_STOP("fockb eigen sol");

					if (!param.localize) {
						bmo = transform(world, bmo, U, trantol, true);
						truncate(world, bmo);
						normalize(world, bmo);
					}
				}

				if (world.rank() == 0) {
					print(" ");
					print("alpha eigenvalues");
					print (aeps);
					if (param.nbeta != 0 && !param.spin_restricted) {
						print("beta eigenvalues");
						print (beps);
					}
				}

				if (param.localize) {
					// Restore the diagonal elements for the analysis
					for (unsigned int i = 0; i < amo.size(); ++i)
						aeps[i] = focka(i, i);
					if (param.nbeta != 0 && !param.spin_restricted)
						for (unsigned int i = 0; i < bmo.size(); ++i)
							beps[i] = fockb(i, i);
				}

				break;
			}

		}

		update_subspace(world, Vpsia, Vpsib, focka, fockb, subspace, Q,
				bsh_residual, update_residual);
	}
	// if(param.absolvent){
	//     rhoT = rhon - rho_elec;
	//     // print("TOTAL CHARGE", rhoT.trace());
	//     DFTSolventSolver DFTSsolver(vacuo_rho,rhoT,param.rho_0,param.epsilon_2,param.maxiter,world,
	//                                 param.Gamma,param.beta,std::max(1e-5,1e-7));
	//     //DFTSsolver.dftsolverplots();
	//     //functionT vsolvent = DFTSsolver.ESP();
	//     //realfunc FF  = DFTSsolver.make_electric_field(vsolvent);
	//     //double Fxyz = DFTSsolver.ave_rxn_field(Uabinit,mol_mask);
	//     double E_es = 0.5*rhoT.inner(Uabinit);
	//     double E_cav = DFTSsolver.cavitation_energy();
	//     double Surface = DFTSsolver.make_surface().trace();
	//     double Volume = DFTSsolver.make_characteristic_func().trace();
	//     double E_free = esol + E_es - vacuo_energy;
	//     double G_sol = E_free + E_cav;
	//     // print("Electrostatic energy ",E_es);

	//     if(world.rank() == 0) {
	//         print("\n\n");
	//         print("                            MADNESS SOLVATION RESULTS           ");
	//         print("                                _________________              \n ");
	//         printf("          (electrostatic) solvation energy: %16.8f %s %12.8f %s\n      ",E_es, "(",E_es*627.503,"kcal/mol)");
	//         printf("(electrostatic)solvation free energy: %16.8f %s %12.8f %s\n     ",E_free, "(",E_free*627.503,"kcal/mol)");
	//         printf("                solvation free energy: %16.8f %s %12.8f %s\n     ",G_sol, "(",G_sol*627.503,"kcal/mol)");
	//         //  print("       solute-solvent reflected field:    ",Fxyz,"a.u.");
	//         printf("                    cavitation energy: %16.8f\n     ",E_cav*627.503);
	//         printf("                     gas phase energy: %16.8f\n     ",vacuo_energy);
	//         printf("                solution phase energy: %16.8f\n     ",esol);
	//         printf("                     Molecular Volume: %16.8f\n     ",Volume);
	//         printf("                    Molecular Surface: %16.8f\n\n     ",Surface);
	//     }
	//  }

	if (world.rank() == 0) {
		if (param.localize)
			print(
					"Orbitals are localized - energies are diagonal Fock matrix elements\n");
		else
			print("Orbitals are eigenvectors - energies are eigenvalues\n");
		print("Analysis of alpha MO vectors");
	}

	analyze_vectors(world, amo, aocc, aeps);
	if (param.nbeta != 0 && !param.spin_restricted) {
		if (world.rank() == 0)
			print("Analysis of beta MO vectors");

		analyze_vectors(world, bmo, bocc, beps);
	}

}        // end solve function

}
