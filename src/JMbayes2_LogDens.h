#ifndef JMBAYES2LOGDENS_H
#define JMBAYES2LOGDENS_H

#include <Rcpp.h>
#include <RcppArmadillo.h>
#include "JMbayes2_Funs.h"
// [[Rcpp::depends("RcppArmadillo")]]

using namespace Rcpp;
using namespace arma;

vec log_long_i (const mat &y_i, const vec &eta_i, const double &sigma_i,
                const double &extr_prm_i, const std::string &fam_i,
                const std::string &link_i, const uvec &idFast_i) {
  uword N = y_i.n_rows;
  vec log_contr(N);
  vec mu_i = mu_fun(eta_i, link_i);
  if (fam_i == "gaussian") {
    log_contr = log_dnorm(y_i, mu_i, sigma_i);
  } else if (fam_i == "Student-t") {
    log_contr = log_dt((y_i - mu_i) / sigma_i, extr_prm_i) - std::log(sigma_i);
  } else if (fam_i == "beta") {
    log_contr = log_dbeta(y_i, mu_i * sigma_i, sigma_i * (1 - mu_i));
  } else if (fam_i == "Gamma") {
    log_contr = log_dgamma(y_i, sigma_i, mu_i / sigma_i);
  } else if (fam_i == "unit Lindley") {
    vec theta = 1 / mu_i - 1;
    vec comp1 = 2 * log(theta) - log(1 + theta);
    vec comp2 = - 3 * log(1 - y_i);
    vec comp3 = - (theta * y_i) / (1 - y_i);
    log_contr = comp1 + comp2 + comp3;
  } else if (fam_i == "binomial") {
    uword k = y_i.n_cols;
    if (k == 2) {
      // y_i.col(1) has been set to y_i.col(0) + y_i.col(1)
      // in jm_fit(), i.e., y_i.col(1) is already the number of trials
      // not the number of failures
      log_contr = log_dbinom(y_i.col(0), y_i.col(1), mu_i);
    } else {
      log_contr = y_i % log(mu_i) + (1 - y_i) % log(1 - mu_i);
    }
  } else if (fam_i == "poisson") {
    log_contr = log_dpois(y_i, mu_i);
  } else if (fam_i == "negative binomial") {
    log_contr = log_dnbinom(y_i, mu_i, sigma_i);
  }  else if (fam_i == "beta binomial") {
    uword k = y_i.n_cols;
    if (k == 2) {
      // y_i.col(1) has been set to y_i.col(0) + y_i.col(1)
      // in jm_fit(), i.e., y_i.col(1) is already the number of trials
      // not the number of failures
      log_contr = log_dbbinom(y_i.col(0), y_i.col(1), mu_i, sigma_i);
    } else {
      vec ones(N, fill::ones);
      log_contr = log_dbbinom(y_i, ones, mu_i, sigma_i);
    }
  }
  vec out = group_sum(log_contr, idFast_i);
  return out;
}

vec log_long (const field<mat> &y, const field<vec> &eta, const vec &sigmas,
              const vec &extra_parms, const CharacterVector &families,
              const CharacterVector &links, const field<uvec> &idFast,
              const field<uvec> &unq_ids) {
  uword n_outcomes = y.size();
  uvec ns(n_outcomes);
  for (uword i = 0; i < n_outcomes; ++i) {
    ns.at(i) = idFast.at(i).n_rows;
  }
  uword n = ns.max();
  vec out(n, fill::zeros);
  for (uword i = 0; i < n_outcomes; ++i) {
    mat y_i = y.at(i);
    vec eta_i = eta.at(i);
    double sigma_i = sigmas.at(i);
    double extr_prm_i = extra_parms.at(i);
    std::string fam_i = std::string(families[i]);
    std::string link_i = std::string(links[i]);
    uvec idFast_i = idFast.at(i);
    uvec unq_id_i = unq_ids.at(i);
    vec log_contr_i = log_long_i(y_i, eta_i, sigma_i, extr_prm_i, fam_i,
                                 link_i, idFast_i);
    out.rows(unq_id_i) += log_contr_i;
  }
  return out;
}

vec log_surv (const vec &W0H_bs_gammas, const vec &W0h_bs_gammas,
                 const vec &W0H2_bs_gammas, const vec &WH_gammas,
                 const vec &Wh_gammas, const vec &WH2_gammas,
                 const vec &WlongH_alphas, const vec &Wlongh_alphas,
                 const vec &WlongH2_alphas, const vec &log_Pwk, const vec &log_Pwk2,
                 const uvec &indFast_H, const uvec &indFast_h, const uvec &which_event,
                 const uvec &which_right_event, const uvec &which_left,
                 const bool &any_interval, const uvec &which_interval) {
  vec lambda_H = W0H_bs_gammas + WH_gammas + WlongH_alphas;
  vec H = group_sum(exp(log_Pwk + lambda_H), indFast_H);
  uword n = H.n_rows;
  vec lambda_h(n);
  lambda_h.rows(which_event) = W0h_bs_gammas.rows(which_event) +
    Wh_gammas.rows(which_event) + Wlongh_alphas.rows(which_event);
  vec out(n);
  out.rows(which_right_event) = - H.rows(which_right_event);
  out.rows(which_event) += lambda_h.rows(which_event);
  out.rows(which_left) = log1p(- exp(- H.rows(which_left)));
  vec lambda_H2(lambda_H.n_rows);
  vec H2(n);
  if (any_interval) {
    lambda_H2 = W0H2_bs_gammas + WH2_gammas + WlongH2_alphas;
    H2 = group_sum(exp(log_Pwk2 + lambda_H2), indFast_H);
    out.rows(which_interval) = - H.rows(which_interval) +
      log(- expm1(- H2.rows(which_interval)));
  }
  out = group_sum(out, indFast_h);
  return out;
}

vec log_re (const mat &b, const mat &L, const vec &sds) {
  mat chol_Sigma = L.each_row() % sds.t();
  vec out = log_dmvnrm_chol(b, chol_Sigma);
  return out;
}

vec logLik (const field<mat> &y, const field<vec> &eta, const vec &sigmas,
               const vec &extra_parms, const CharacterVector &families,
               const CharacterVector &links, const field<uvec> &ids,
               const field<uvec> &unq_ids,
               const vec &W0H_bs_gammas, const vec &W0h_bs_gammas,
               const vec &W0H2_bs_gammas, const vec &WH_gammas,
               const vec &Wh_gammas, const vec &WH2_gammas,
               const vec &WlongH_alphas, const vec &Wlongh_alphas,
               const vec &WlongH2_alphas, const vec &log_Pwk, const vec &log_Pwk2,
               const uvec &indFast_H, const uvec &indFast_h,
               const uvec &which_event,
               const uvec &which_right_event, const uvec &which_left,
               const bool &any_interval, const uvec &which_interval,
               const mat &b_mat, const mat &L, const vec &sds) {
  vec logLik_long = log_long(y, eta, sigmas, extra_parms, families, links, ids,
                             unq_ids);
  vec logLik_surv = log_surv(W0H_bs_gammas, W0h_bs_gammas, W0H2_bs_gammas,
                             WH_gammas, Wh_gammas, WH2_gammas, WlongH_alphas,
                             Wlongh_alphas, WlongH2_alphas, log_Pwk,
                             log_Pwk2, indFast_H, indFast_h, which_event,
                             which_right_event, which_left, any_interval,
                             which_interval);
  vec logLik_re = log_re(b_mat, L, sds);
  vec out = logLik_long + logLik_surv + logLik_re;
  return out;
}

double logLik_prior (const mat &L, const vec &sds,
                     const double &prior_D_sds_df, const double &prior_D_sds_sigma,
                     const double &prior_D_L_etaLKJ,
                     const vec &bs_gammas, const vec &gammas, const vec &alphas,
                     const vec &prior_mean_bs_gammas, const mat &prior_Tau_bs_gammas,
                     const vec &prior_mean_gammas, const mat &prior_Tau_gammas,
                     const vec &prior_mean_alphas, const mat &prior_Tau_alphas,
                     const double &tau_bs_gammas,
                     double prior_A_tau_bs_gammas, double prior_B_tau_bs_gammas) {
  double out(0.0);
  out += sum(log_dht(sds, prior_D_sds_sigma, prior_D_sds_df));
  uword p = L.n_rows;
  double log_p_L(0.0);
  for (unsigned i = 1; i < p; ++i) {
    log_p_L += (p - i - 1.0 + 2.0 * prior_D_L_etaLKJ - 2.0) * log(L.at(i, i));
  }
  out += log_p_L;
  out += logPrior(bs_gammas, prior_mean_bs_gammas, prior_Tau_bs_gammas,
                  tau_bs_gammas);
  out += logPrior(gammas, prior_mean_gammas, prior_Tau_gammas, 1.0);
  out += logPrior(alphas, prior_mean_alphas, prior_Tau_alphas, 1.0);

  return out;
}

#endif
