#ifndef JMBAYES2FUNS
#define JMBAYES2FUNS

#include <Rcpp.h>
#include <RcppArmadillo.h>
// [[Rcpp::depends("RcppArmadillo")]]

using namespace Rcpp;
using namespace arma;

double robbins_monro (const double &scale, const double &acceptance_it,
                      const int &it, const double &target_acceptance = 0.45) {
  double step_length = scale / (target_acceptance * (1 - target_acceptance));
  double out;
  if (acceptance_it > 0) {
    out = scale + step_length * (1 - target_acceptance) / (double)it;
  } else {
    out = scale - step_length * target_acceptance / (double)it;
  }
  return out;
}

void inplace_UpperTrimat_mult (rowvec &x, const mat &trimat) {
  // in-place multiplication of x with an upper triangular matrix trimat
  // because in-place assignment is much faster but careful in use because
  // it changes the input vector x, i.e., not const
  uword const n = trimat.n_cols;
  for (uword j = n; j-- > 0;) {
    double tmp(0.0);
    for (uword i = 0; i <= j; ++i)
      tmp += trimat.at(i, j) * x.at(i);
    x.at(j) = tmp;
  }
}

void inplace_LowerTrimat_mult (rowvec &x, const mat &trimat) {
  // in-place multiplication of x with an lower triangular matrix trimat
  // because in-place assignment is much faster but careful in use because
  // it changes the input vector x, i.e., not const
  uword const n = trimat.n_cols;
  for (uword j = 0; j < n; ++j) {
    double tmp(0.0);
    for (uword i = j; i < n; ++i)
      tmp += trimat.at(i, j) * x.at(i);
    x.at(j) = tmp;
  }
}

mat cov2cor (const mat &V) {
  vec Is = sqrt(1 / V.diag());
  mat out = V.each_col() % Is;
  out.each_row() %= Is.t();
  return out;
}

mat cor2cov (const mat &R, const vec &sds) {
  mat out = R.each_col() % sds;
  out.each_row() %= sds.t();
  return out;
}

vec group_sum (const vec &x, const uvec &ind) {
  vec cumsum_x = cumsum(x);
  vec out = cumsum_x.elem(ind);
  out.insert_rows(0, 1);
  out = diff(out);
  return out;
}

vec create_init_scale(const uword &n, const double &fill_val = 0.1) {
  vec out(n);
  out.fill(fill_val);
  return out;
}

field<mat> List2Field_mat (const List &Mats) {
  int n_list = Mats.size();
  field<mat> res(n_list);
  for (int i = 0; i < n_list; ++i) {
    res.at(i) = as<mat>(Mats[i]);
  }
  return res;
}

field<vec> List2Field_vec (const List &Vecs) {
  int n_list = Vecs.size();
  field<vec> res(n_list);
  for (int i = 0; i < n_list; ++i) {
    res.at(i) = as<vec>(Vecs[i]);
  }
  return res;
}

field<mat> create_storage(const field<vec> &F, const int &n_iter) {
  int n = F.size();
  field<mat> out(n);
  for (int i = 0; i < n; ++i) {
    vec aa = F.at(i);
    int n_i = aa.n_rows;
    mat tt(n_iter, n_i, fill::zeros);
    out.at(i) = tt;
  }
  return out;
}

vec Wlong_alphas_fun(const field<mat> &Mats, const field<vec> &coefs) {
  int n = Mats.size();
  int n_rows = Mats.at(0).n_rows;
  vec out(n_rows, fill::zeros);
  for (int k = 0; k < n; ++k) {
    out += Mats.at(k) * coefs.at(k);
  }
  return out;
}

mat docall_cbind (const List &Mats_) {
  field<mat> Mats = List2Field_mat(Mats_);
  int n = Mats.size();
  uvec ncols(n);
  for (int k = 0; k < n; ++k) {
    ncols.at(k) = Mats.at(k).n_cols;
  }
  int N = sum(ncols);
  int col_start = 0;
  int col_end = ncols.at(0) - 1;
  mat out(Mats.at(0).n_rows, N);
  for (int k = 0; k < n; ++k) {
    if (k > 0) {
      col_start += ncols.at(k - 1);
      col_end += ncols.at(k);
    }
    out.cols(col_start, col_end) = Mats.at(k);
  }
  return out;
}

uvec create_fast_ind (const uvec &group) {
  unsigned int l = group.n_rows;
  uvec ind = find(group.rows(1, l - 1) != group.rows(0, l - 2));
  unsigned int k = ind.n_rows;
  ind.insert_rows(k, 1);
  ind.at(k) = l - 1;
  return ind;
}

double logPrior(const vec &x, const vec &mean, const mat &Tau,
                const double tau = 1.0) {
  vec z = (x - mean);
  double out = - 0.5 * tau * as_scalar(z.t() * Tau * z);
  return out;
}


#endif