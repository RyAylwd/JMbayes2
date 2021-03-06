#include <RcppArmadillo.h>
// [[Rcpp::depends("RcppArmadillo")]]

using namespace Rcpp;
using namespace arma;

arma::vec group_sum (const arma::vec& x, const arma::uvec& ind) {
    arma::vec cumsum_x = cumsum(x);
    arma::vec out = cumsum_x.elem(ind);
    out.insert_rows(0, 1);
    out = diff(out);
    return(out);
}

arma::field<arma::mat> List2Field_mat (const List& Mats) {
    int n_list = Mats.size();
    arma::field<arma::mat> res(n_list);
    for (int i = 0; i < n_list; ++i) {
        res.at(i) = as<arma::mat>(Mats[i]);
    }
    return(res);
}

arma::field<arma::vec> List2Field_vec (const List& Vecs) {
    int n_list = Vecs.size();
    arma::field<arma::vec> res(n_list);
    for (int i = 0; i < n_list; ++i) {
        res.at(i) = as<arma::vec>(Vecs[i]);
    }
    return(res);
}

arma::field<arma::mat> create_storage(const arma::field<arma::vec>& F,
                                      const int& n_iter) {
    int n = F.size();
    arma::field<arma::mat> out(n);
    for (int i = 0; i < n; ++i) {
        arma::vec aa = F.at(i);
        int n_i = aa.n_rows;
        arma::mat tt(n_iter, n_i, fill::zeros);
        out.at(i) = tt;
    }
    return(out);
}

arma::vec Wlong_alphas_fun (const arma::field<arma::mat>& Mats,
                            const arma::field<arma::vec>& coefs) {
    int n = Mats.size();
    int n_rows = Mats.at(0).n_rows;
    arma::vec out(n_rows, fill::zeros);
    for (int i = 0; i < n; ++i) {
        out += Mats.at(i) * coefs.at(i);
    }
    return(out);
}

arma::uvec create_fast_ind (const arma::uvec& group) {
    unsigned int l = group.n_rows;
    arma::uvec ind = find(group.rows(1, l - 1) != group.rows(0, l - 2));
    unsigned int k = ind.n_rows;
    ind.insert_rows(k, 1);
    ind.at(k) = l - 1;
    return(ind);
}

double logPrior(const vec& x, const vec& mean, const mat& Tau, double tau = 1) {
    vec z = (x - mean);
    return(- 0.5 * tau * as_scalar(z.t() * Tau * z));
}

arma::vec propose (const arma::vec& thetas, const arma::vec& scale, const int& it) {
    arma::vec proposed_thetas = thetas;
    proposed_thetas.at(it) = R::rnorm(thetas.at(it), scale.at(it));
    return(proposed_thetas);
}

double robbins_monro (const double& scale, const double& acceptance_it,
                      const int& it, const double& target_acceptance = 0.45) {
    double step_length = scale / (target_acceptance * (1 - target_acceptance));
    double out;
    if (acceptance_it > 0) {
        out = scale + step_length * (1 - target_acceptance) / (double)it;
    } else {
        out = scale - step_length * target_acceptance / (double)it;
    }
    return out;
}

double robbins_monro_mv (const double& scale, const double& acceptance_it,
                         const int& it, const int& dim,
                         const double& target_acceptance = 0.25) {
    double it2 = (double)it;
    double dim2 = (double)dim;
    double A = 1 - 1 / dim2;
    double alpha = - R::qnorm(target_acceptance  / 2, 0.0, 1.0, 1, 0);
    double B = 0.5 * pow(2 * 3.141592654, 0.5) * exp(0.5 * pow(alpha, 2)) / alpha;
    double C = 1 / (dim2 * target_acceptance * (1 - target_acceptance));
    double step_length = scale * (A * B + C);
    double den;
    if (it > 299) {
        den = max(NumericVector::create(299.0, it2 / dim2));
    } else {
        den = it2;
    }
    double out;
    if (acceptance_it > 0) {
        out = scale + step_length * (1 - target_acceptance) / den;
    } else {
        out = scale - step_length * target_acceptance / den;
    }
    return out;
}

arma::vec mvrnorm(const arma::vec& mean, const arma::mat& var_cov) {
    int n = var_cov.n_cols;
    vec Y = randn(n);
    mat H = chol(var_cov);
    Y = mean + H.t() * Y;
    return Y;
}

double log_density_surv (const arma::vec& W0H_bs_gammas,
                         const arma::vec& W0h_bs_gammas,
                         const arma::vec& W0H2_bs_gammas,
                         const arma::vec& WH_gammas,
                         const arma::vec& Wh_gammas,
                         const arma::vec& WH2_gammas,
                         const arma::vec& WlongH_alphas,
                         const arma::vec& Wlongh_alphas,
                         const arma::vec& WlongH2_alphas,
                         const arma::vec& log_Pwk, const arma::vec& log_Pwk2,
                         const arma::uvec& indFast_H,
                         const arma::uvec& which_event,
                         const arma::uvec& which_right_event,
                         const arma::uvec& which_left,
                         const bool& any_interval,
                         const arma::uvec& which_interval) {
    arma::vec lambda_H = W0H_bs_gammas + WH_gammas + WlongH_alphas;
    arma::vec H = group_sum(exp(log_Pwk + lambda_H), indFast_H);
    int n = H.n_rows;
    arma::vec lambda_h(n);
    lambda_h.elem(which_event) = W0h_bs_gammas.elem(which_event) +
        Wh_gammas.elem(which_event) + Wlongh_alphas.elem(which_event);
    arma::vec log_Lik_surv(n);
    log_Lik_surv.elem(which_right_event) = - H.elem(which_right_event);
    log_Lik_surv.elem(which_event) += lambda_h.elem(which_event);
    log_Lik_surv.elem(which_left) = log1p(- exp(- H.elem(which_left)));
    arma::vec lambda_H2(lambda_H.n_rows);
    arma::vec H2(n);
    if (any_interval) {
        lambda_H2 = W0H2_bs_gammas + WH2_gammas + WlongH2_alphas;
        H2 = group_sum(exp(log_Pwk2 + lambda_H2), indFast_H);
        log_Lik_surv.elem(which_interval) = - H.elem(which_interval) +
            log(- expm1(- H2.elem(which_interval)));
    }
    return(sum(log_Lik_surv));
}


// [[Rcpp::export]]
List mcmc (List model_data, List model_info, List initial_values,
           List priors, List vcov_prop, List control) {
    // outcome vectors and design matrices
    vec Time_right = as<vec>(model_data["Time_right"]);
    vec Time_left = as<vec>(model_data["Time_left"]);
    vec Time_start = as<vec>(model_data["Time_start"]);
    vec delta = as<vec>(model_data["Time_start"]);
    uvec which_event = as<uvec>(model_data["which_event"]) - 1;
    uvec which_right = as<uvec>(model_data["which_right"]) - 1;
    uvec which_right_event = join_cols(which_event, which_right);
    uvec which_left = as<uvec>(model_data["which_left"]) - 1;
    uvec which_interval = as<uvec>(model_data["which_interval"]) - 1;
    mat W0_H = as<mat>(model_data["W0_H"]);
    mat W0_h = as<mat>(model_data["W0_h"]);
    mat W0_H2 = as<mat>(model_data["W0_H2"]);
    mat W_H = as<mat>(model_data["W_H"]);
    mat W_h = as<mat>(model_data["W_h"]);
    mat W_H2 = as<mat>(model_data["W_H2"]);
    mat W_bar = as<mat>(model_data["W_bar"]);
    List Wlong_H_ = as<List>(model_data["Wlong_H"]);
    field<mat> Wlong_H = List2Field_mat(Wlong_H_);
    List Wlong_h_ = as<List>(model_data["Wlong_h"]);
    field<mat> Wlong_h = List2Field_mat(Wlong_h_);
    List Wlong_H2_ = as<List>(model_data["Wlong_H2"]);
    field<mat> Wlong_H2 = List2Field_mat(Wlong_H2_);
    // other information
    int n_outcomes = Wlong_H.size();
    uvec idT = as<uvec>(model_data["idT"]) - 1;
    vec log_Pwk = as<vec>(model_data["log_Pwk"]);
    vec log_Pwk2 = as<vec>(model_data["log_Pwk2"]);
    uvec id_H = create_fast_ind(as<uvec>(model_data["id_H"]));
    uvec id_H2 = create_fast_ind(as<uvec>(model_data["id_H2"]));
    bool any_gammas = as<bool>(model_data["any_gammas"]);
    bool any_event = which_event.n_rows > 0;
    bool any_interval = which_interval.n_rows > 0;
    // initial values
    List betas_ = as<List>(initial_values["betas"]);
    field<vec> betas = List2Field_vec(betas_);
    List b_ = as<List>(initial_values["b"]);
    field<mat> b = List2Field_mat(b_);
    vec bs_gammas = as<vec>(initial_values["bs_gammas"]);
    vec gammas = as<vec>(initial_values["gammas"]);
    List alphas_ = as<List>(initial_values["alphas"]);
    field<vec> alphas = List2Field_vec(alphas_);
    double tau_bs_gammas = as<double>(initial_values["tau_bs_gammas"]);
    // MCMC settings
    int n_iter = as<int>(control["n_iter"]);
    int n_burnin = as<int>(control["n_burnin"]);
    // priors
    vec prior_mean_bs_gammas = as<vec>(priors["mean_bs_gammas"]);
    mat prior_Tau_bs_gammas = as<mat>(priors["Tau_bs_gammas"]);
    vec prior_mean_gammas = as<vec>(priors["mean_gammas"]);
    mat prior_Tau_gammas = as<mat>(priors["Tau_gammas"]);
    vec prior_mean_alphas = as<vec>(priors["mean_alphas"]);
    mat prior_Tau_alphas = as<mat>(priors["Tau_alphas"]);
    double post_A_tau_bs_gammas = as<double>(priors["A_tau_bs_gammas"]) +
        0.5 * as<double>(priors["rank_Tau_bs_gammas"]);
    double prior_B_tau_bs_gammas = as<double>(priors["B_tau_bs_gammas"]);
    // scales
    mat Sigma_bs_gammas = as<mat>(vcov_prop["vcov_prop_bs_gammas"]);
    double scale_bs_gammas = 0.1;
    vec scale_gammas(gammas.n_rows, fill::ones);
    scale_gammas = 0.1 * scale_gammas;
    field<vec> scale_alphas = alphas;
    for (int i = 0; i < n_outcomes; ++i) {
        scale_alphas.at(i) = scale_alphas.at(i) * 0 + 0.1;
    }
    // store results
    int n_bs_gammas = bs_gammas.n_rows;
    int n_gammas = gammas.n_rows;
    mat res_bs_gammas(n_iter, n_bs_gammas);
    vec acceptance_bs_gammas(n_iter, fill::zeros);
    mat res_gammas(n_iter, n_gammas);
    vec res_W_bar_gammas(n_iter);
    mat acceptance_gammas(n_iter, n_gammas, fill::zeros);
    field<mat> res_alphas = create_storage(alphas, n_iter);
    field<mat> acceptance_alphas = create_storage(alphas, n_iter);
    vec res_tau_bs_gammas(n_iter);
    // preliminaries
    vec W0H_bs_gammas = W0_H * bs_gammas;
    vec W0h_bs_gammas(W0_h.n_rows);
    vec W0H2_bs_gammas(W0_H2.n_rows);
    if (any_event) {
        W0h_bs_gammas = W0_h * bs_gammas;
    }
    if (any_interval) {
        W0H2_bs_gammas = W0_H2 * bs_gammas;
    }
    vec WH_gammas(W0_H.n_rows);
    vec Wh_gammas(W0_h.n_rows);
    vec WH2_gammas(W0_H2.n_rows);
    if (any_gammas) {
        WH_gammas = W_H * gammas;
    }
    if (any_gammas && any_event) {
        Wh_gammas = W_h * gammas;
    }
    if (any_gammas && any_interval) {
        WH2_gammas = WH2_gammas * gammas;
    }
    vec WlongH_alphas = Wlong_alphas_fun(Wlong_H, alphas);
    vec Wlongh_alphas(W0_h.n_rows);
    if (any_event) {
        Wlongh_alphas = Wlong_alphas_fun(Wlong_h, alphas);
    }
    vec WlongH2_alphas(W0_H2.n_rows);
    if (any_interval) {
        WlongH2_alphas = Wlong_alphas_fun(Wlong_H2, alphas);
    }
    double denominator_surv =
        log_density_surv(W0H_bs_gammas, W0h_bs_gammas, W0H2_bs_gammas,
                         WH_gammas, Wh_gammas, WH2_gammas,
                         WlongH_alphas, Wlongh_alphas, WlongH2_alphas,
                         log_Pwk, log_Pwk2, id_H,
                         which_event, which_right_event, which_left,
                         any_interval, which_interval) +
        logPrior(bs_gammas, prior_mean_bs_gammas, prior_Tau_bs_gammas, tau_bs_gammas) +
        logPrior(gammas, prior_mean_gammas, prior_Tau_gammas, 1);
    for (int it = 0; it < n_iter; ++it) {
        vec proposed_bs_gammas = mvrnorm(bs_gammas,
                                         scale_bs_gammas * Sigma_bs_gammas);
        vec proposed_W0H_bs_gammas = W0_H * proposed_bs_gammas;
        vec proposed_W0h_bs_gammas(W0_h.n_rows);
        vec proposed_W0H2_bs_gammas(W0_H2.n_rows);
        if (any_event) {
            proposed_W0h_bs_gammas = W0_h * proposed_bs_gammas;
        }
        if (any_interval) {
            proposed_W0H2_bs_gammas = W0_H2 * proposed_bs_gammas;
        }
        double numerator_surv =
            log_density_surv(proposed_W0H_bs_gammas, proposed_W0h_bs_gammas, proposed_W0H2_bs_gammas,
                             WH_gammas, Wh_gammas, WH2_gammas,
                             WlongH_alphas, Wlongh_alphas, WlongH2_alphas,
                             log_Pwk, log_Pwk2, id_H,
                             which_event, which_right_event, which_left,
                             any_interval, which_interval) +
            logPrior(proposed_bs_gammas, prior_mean_bs_gammas, prior_Tau_bs_gammas, tau_bs_gammas) +
            logPrior(gammas, prior_mean_gammas, prior_Tau_gammas, 1);
        double log_ratio = numerator_surv - denominator_surv;
        if (is_finite(log_ratio) && exp(log_ratio) > R::runif(0, 1)) {
            bs_gammas = proposed_bs_gammas;
            W0H_bs_gammas = proposed_W0H_bs_gammas;
            if (any_event) {
                W0h_bs_gammas = proposed_W0h_bs_gammas;
            }
            if (any_interval) {
                W0H2_bs_gammas = proposed_W0H2_bs_gammas;
            }
            denominator_surv = numerator_surv;
            acceptance_bs_gammas.at(it) = 1;
        }
        if (it > 19) {
            scale_bs_gammas =
                robbins_monro_mv(scale_bs_gammas, acceptance_bs_gammas.at(it),
                                 it, n_bs_gammas);
        }
        if (it > 299) {
            double g = 0.3; //0.3 / ((double)it - 299);
            Sigma_bs_gammas = g * Sigma_bs_gammas +
                (1 - g) * cov(res_bs_gammas.rows(0, it));
        }
        res_bs_gammas.row(it) = bs_gammas.t();
        double post_B_tau_bs_gammas = prior_B_tau_bs_gammas +
            0.5 * as_scalar(bs_gammas.t() * prior_Tau_bs_gammas * bs_gammas);
        tau_bs_gammas = R::rgamma(post_A_tau_bs_gammas, 1 / post_B_tau_bs_gammas);
        res_tau_bs_gammas.at(it) = tau_bs_gammas;
        if (any_gammas) {
            for (int i = 0; i < n_gammas; ++i) {
                vec proposed_gammas = propose(gammas, scale_gammas, i);
                vec proposed_WH_gammas = W_H * proposed_gammas;
                vec proposed_Wh_gammas(W_h.n_rows);
                vec proposed_WH2_gammas(W_H2.n_rows);
                if (any_event) {
                    proposed_Wh_gammas = W_h * proposed_gammas;
                }
                if (any_interval) {
                    proposed_WH2_gammas = W_H2 * proposed_gammas;
                }
                double numerator_surv =
                    log_density_surv(W0H_bs_gammas, W0h_bs_gammas, W0H2_bs_gammas,
                                     proposed_WH_gammas, proposed_Wh_gammas, proposed_WH2_gammas,
                                     WlongH_alphas, Wlongh_alphas, WlongH2_alphas,
                                     log_Pwk, log_Pwk2, id_H,
                                     which_event, which_right_event, which_left,
                                     any_interval, which_interval) +
                    logPrior(bs_gammas, prior_mean_bs_gammas, prior_Tau_bs_gammas, tau_bs_gammas) +
                    logPrior(proposed_gammas, prior_mean_gammas, prior_Tau_gammas, 1);
                double log_ratio = numerator_surv - denominator_surv;
                if (is_finite(log_ratio) && exp(log_ratio) > R::runif(0, 1)) {
                    gammas = proposed_gammas;
                    WH_gammas = proposed_WH_gammas;
                    if (any_event) {
                        Wh_gammas = proposed_Wh_gammas;
                    }
                    if (any_interval) {
                        WH2_gammas = proposed_WH2_gammas;
                    }
                    denominator_surv = numerator_surv;
                    acceptance_gammas.at(it, i) = 1;
                }
                if (it > 19) {
                    scale_gammas.at(i) =
                        robbins_monro(scale_gammas.at(i),
                                      acceptance_gammas.at(it, i), it);
                }
                // store results
                res_gammas.at(it, i) = gammas.at(i);
            }
            res_W_bar_gammas.at(it) = as_scalar(W_bar * gammas);
        }
    }
    return List::create(
        Named("mcmc") = List::create(
            Named("bs_gammas") = res_bs_gammas.rows(n_burnin, n_iter - 1),
            Named("tau_bs_gammas") = res_tau_bs_gammas.rows(n_burnin, n_iter - 1),
            Named("gammas") = res_gammas.rows(n_burnin, n_iter - 1),
            Named("W_bar_gammas") = res_W_bar_gammas.rows(n_burnin, n_iter - 1)
        ),
        Named("acc_rate") = List::create(
            Named("bs_gammas") = acceptance_bs_gammas,
            Named("gammas") = acceptance_gammas
        )
    );
}



