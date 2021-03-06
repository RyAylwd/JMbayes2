library("survival")
library("nlme")
library("GLMMadaptive")
library("splines")
data("pbc2", package = "JM")
data("pbc2.id", package = "JM")
source(file.path(getwd(), "R/jm.R"))
source(file.path(getwd(), "R/help_functions.R"))
source(file.path(getwd(), "Development/jm/R_to_Cpp.R"))
source(file.path(getwd(), "Development/jm/PBC_data.R"))

fm1 <- lme(log(serBilir) ~ year * sex + I(year^2) + age + prothrombin,
           data = pbc2, random = ~ year | id)
fm2 <- lme(serChol ~ ns(year, 3, B = c(0, 10)) + sex + age, data = pbc2,
           random = ~ year | id, na.action = na.exclude)
fm2. <- lme(I(serChol / 150) ~ ns(year, 2, B = c(0, 10)) + sex + age, data = pbc2,
           random = ~ ns(year, 2, B = c(0, 10)) | id, na.action = na.exclude)
fm3 <- mixed_model(hepatomegaly ~ sex + age, data = pbc2,
                   random = ~ 1 | id, family = binomial())
fm4 <- mixed_model(ascites ~ year + age, data = pbc2,
                   random = ~ year | id, family = binomial())
fm5 <- lme(prothrombin ~ ns(year, 2, B = c(0, 10)) * drug, data = pbc2,
           random = ~ ns(year, 2, B = c(0, 10)) | id,
           control = lmeControl(opt = "optim"))

Mixed_objects <- list(fm1, fm2., fm3, fm4, fm5, fm1)

D_lis <- lapply(Mixed_objects, extract_D)
D <- bdiag(D_lis)

##########################################################################################
##########################################################################################

dmvnorm <- function (x, mu, Sigma = NULL, invSigma = NULL, log = TRUE,
          prop = TRUE) {
    if (!is.matrix(x))
        x <- rbind(x)
    if (is.matrix(mu)) {
        if (nrow(mu) != nrow(x))
            stop("incorrect dimensions for 'mu'.")
        p <- ncol(mu)
    }
    else {
        p <- length(mu)
        mu <- rep(mu, each = nrow(x))
    }
    if (is.null(Sigma) && is.null(invSigma))
        stop("'Sigma' or 'invSigma' must be given.")
    if (!is.null(Sigma)) {
        if (is.list(Sigma)) {
            ev <- Sigma$values
            evec <- Sigma$vectors
        } else {
            ed <- eigen(Sigma, symmetric = TRUE)
            ev <- ed$values
            evec <- ed$vectors
        }
        invSigma <- evec %*% (t(evec)/ev)
        if (!prop)
            logdetSigma <- sum(log(ev))
    } else {
        if (!prop)
            logdetSigma <- -determinant(as.matrix(invSigma))$modulus
    }
    ss <- x - mu
    quad <- 0.5 * rowSums((ss %*% invSigma) * ss)
    if (!prop)
        fact <- -0.5 * (p * log(2 * pi) + logdetSigma)
    if (log) {
        if (!prop)
            as.vector(fact - quad)
        else as.vector(-quad)
    } else {
        if (!prop)
            as.vector(exp(fact - quad))
        else as.vector(exp(-quad))
    }
}

dmvnorm_chol <- function (x, mu, chol_Sigma = NULL, chol_inv_Sigma = NULL, log = FALSE) {
    if (!is.matrix(x))
        x <- rbind(x)
    if (is.matrix(mu)) {
        if (nrow(mu) != nrow(x))
            stop("incorrect dimensions for 'mu'.")
        p <- ncol(mu)
    } else {
        p <- length(mu)
        mu <- rep(mu, each = nrow(x))
    }
    if (is.null(chol_Sigma) && is.null(chol_inv_Sigma))
        stop("'chol_Sigma' or 'chol_inv_Sigma' must be given.")
    invSigma <- if (!is.null(chol_Sigma)) {
        tcrossprod(backsolve(r = chol_Sigma, x = diag(p)))
    } else {
        crossprod(chol_inv_Sigma)
    }
    logdetSigma <- if (!is.null(chol_Sigma)) {
        2 * determinant(chol_Sigma)$modulus
    } else {
        - 2 * determinant(chol_inv_Sigma)$modulus
    }
    ss <- x - mu
    quad <- 0.5 * rowSums((ss %*% invSigma) * ss)
    fact <- -0.5 * (p * log(2 * pi) + logdetSigma)
    if (log) as.vector(fact - quad) else as.vector(exp(fact - quad))
}

cor2cov <- function (R, vars, sds = NULL) {
    p <- nrow(R)
    if (is.null(sds)) sds <- sqrt(vars)
    sds * R * rep(sds, each = p)
}

ddirichlet <- function (x, alpha, log = FALSE) {
    dirichlet1 <- function(x, alpha) {
        logD <- sum(lgamma(alpha)) - lgamma(sum(alpha))
        s <- sum((alpha - 1) * log(x))
        exp(sum(s) - logD)
    }
    if (!is.matrix(x))
        if (is.data.frame(x))
            x <- as.matrix(x)
        else x <- t(x)
        if (!is.matrix(alpha))
            alpha <- matrix(alpha, ncol = length(alpha), nrow = nrow(x),
                            byrow = TRUE)
        if (any(dim(x) != dim(alpha)))
            stop("Mismatch between dimensions of x and alpha in ddirichlet().\n")
        pd <- vector(length = nrow(x))
        for (i in 1:nrow(x)) pd[i] <- dirichlet1(x[i, ], alpha[i,
                                                               ])
        pd[apply(x, 1, function(z) any(z < 0 | z > 1))] <- 0
        pd[apply(x, 1, function(z) all.equal(sum(z), 1) != TRUE)] <- 0
        if (log) return(log(pd)) else return(pd)
}

rdirichlet <- function (n, alpha) {
    l <- length(alpha)
    x <- matrix(rgamma(l * n, alpha), ncol = l, byrow = TRUE)
    x / rowSums(x)
}

update_scale <- function (scale, rate, target_acc, it, c1 = 0.8, c0 = 1) {
    g1 <- (it + 1)^(1 - c1)
    g2 <- c0 * g1
    exp(log(scale) + g2 * (rate - target_acc))
}

robbins_monro_univ <- function (scale, acceptance_it, it, target_acceptance = 0.45) {
    step_length <- scale / (target_acceptance * (1 - target_acceptance))
    if (acceptance_it) {
        scale + step_length * (1 - target_acceptance) / it
    } else {
        scale - step_length * target_acceptance / it
    }
}

dht <- function (x, sigma = 10, df = 1, log = FALSE) {
    ind <- x > 0
    out <- rep(as.numeric(NA), length(x))
    log_const <- log(2) + lgamma(0.5 * (df + 1)) - lgamma(0.5 * df) -
        0.5 * (log(df) + log(pi)) - log(sigma)
    log_kernel <- - 0.5 * (df + 1) * log(1 + x[ind]^2 / (df * sigma^2))
    out[ind] <- log_const + log_kernel
    if (log) out else exp(out)
}

##########################################################################################
##########################################################################################

# sampling using the half-t approach
# including robbins_monro adaptive scaling

p <- ncol(D)
R <- cov2cor(D)
inv_R <- solve(R)
sds <- sqrt(diag(D))
init_sds <- sds

D <- cor2cov(R, sds = sds)

b <- MASS::mvrnorm(3000, rep(0, p), D)

target_log_dist <- function (sds) {
    p <- length(sds)
    inv_D <- cor2cov(inv_R, sds = 1 / sds)
    log_p_b <- sum(dmvnorm(b, rep(0, p), invSigma = inv_D, log = TRUE, prop = FALSE))
    log_p_tau <- sum(dht(sds, sigma = 10 * init_sds, df = 3, log = TRUE))
    log_p_b + log_p_tau
}

M <- 4000L
acceptance_sds <- res_sds <- matrix(0.0, M, p)
current_sds <- sds
scale_sds <- rep(0.1, p)
system.time({
for (m in seq_len(M)) {
    for (i in seq_len(p)) {
        current_sds_i <- current_sds[i]
        scale_sds_i <- scale_sds[i]
        log_mu_i <- log(current_sds_i) - 0.5 * scale_sds_i^2
        proposed_sds_i <- rlnorm(1L, log_mu_i, scale_sds_i)
        pr <- current_sds
        pr[i] <- proposed_sds_i
        numerator_i <- target_log_dist(pr) +
            dlnorm(current_sds_i, log_mu_i, scale_sds_i, log = TRUE)
        denominator_i <- target_log_dist(current_sds) +
            dlnorm(proposed_sds_i, log_mu_i, scale_sds_i, log = TRUE)
        log_ratio_i <- numerator_i - denominator_i
        if (log_ratio_i > log(runif(1))) {
            current_sds <- pr
            acceptance_sds[m, i] <- 1
        }
        res_sds[m, i] <- current_sds[i]
        if (m > 20) {
            scale_sds[i] <- robbins_monro_univ(scale = scale_sds_i,
                                               acceptance_it = acceptance_sds[m, i],
                                               it = m)
        }
    }
}
})

colMeans(acceptance_sds[-seq_len(1000L), ])

####

res_sds <- res_sds[-seq_len(1000L), ]
plot(res_sds[, 1], type = "l")
plot(res_sds[, 2], type = "l")
plot(res_sds[, 3], type = "l")
plot(res_sds[, 4], type = "l")
plot(res_sds[, 5], type = "l")
plot(res_sds[, 6], type = "l")
plot(res_sds[, 7], type = "l")
plot(res_sds[, 8], type = "l")
plot(res_sds[, 9], type = "l")
plot(res_sds[, 10], type = "l")


####

mean_sds <- colMeans(res_sds)

cbind(mean_sds, sds)

##########################################################################################
##########################################################################################


p <- ncol(D)
K <- as.integer(round(p * (p - 1) / 2))
#D[D == 0] <- 1e-06
sds <- sqrt(diag(D))
R <- cov2cor(D); dimnames(R) <- dimnames(D) <- NULL
inv_R <- solve(R)
L <- chol(R)
upper_tri_ind <- upper.tri(L)
upper_tri_spl <- rep(1:(p-1), 1:(p-1))
diags <- cbind(2:p, 2:p)

b <- MASS::mvrnorm(K * 15, rep(0, p), D)

target_log_dist <- function (L) {
    log_p_b <- sum(dmvnorm_chol(b, rep(0, p), chol_Sigma = L * rep(sds, each = p),
                                log = TRUE))
    log_p_L1 <- sum(dunif(L[1, -1], -1, 1, log = TRUE))
    spl <- split(L[upper_tri_ind], upper_tri_spl)[-1]
    f <- function (l) sqrt(1 - cumsum(l^2))
    limits_upp <- lapply(lapply(spl, head, n = -1), f)
    limits_low <- lapply(limits_upp, function (l) -l)
    log_p_L2 <- mapply(dunif, x = lapply(spl, tail, n = -1),
                       min = limits_low, max = limits_upp,
                       MoreArgs = list(log = TRUE))
    log_p_L2 <- sum(unlist(log_p_L2, use.names = FALSE))
    log_p_b + log_p_L1 + log_p_L2
}

target_log_dist <- function (L, eta = 2) {
    diags <- diag(L)
    if (any(is.na(diags))) return(-Inf)
    test_PD_1 <- all(diags > 0)
    test_PD_2 <- all(abs(sqrt(colSums(L^2)) - 1) < sqrt(.Machine$double.eps))
    if (!test_PD_1 || !test_PD_2) return(-Inf)
    log_p_b <- sum(dmvnorm_chol(b, rep(0, p), chol_Sigma = L * rep(sds, each = p),
                                log = TRUE))
    log_p_L <- sum((p - 2:p + 2 * eta - 2) * log(diags[-1]))
    log_p_b + log_p_L
}


M <- 3000L
res_L <- acceptance_L <- matrix(0.0, M, K)
res_R <- vector("list", M)
scale_L <- rep(0.1, K)
current_L <- L
system.time({
    for (m in seq_len(M)) {
        for (i in seq_len(K)) {
            current_L_i <- current_L[upper_tri_ind][i]
            scale_L_i <- scale_L[i]
            proposed_L_i <- runif(1L, min = current_L_i - 0.5 * scale_L_i * sqrt(12),
                                  max = current_L_i + 0.5 * scale_L_i * sqrt(12))
            pr <- current_L
            pr[upper_tri_ind][i] <- proposed_L_i
            pr[diags] <- sapply(split(pr[upper_tri_ind], upper_tri_spl),
                                       function (x) sqrt(1 - sum(x^2)))
            numerator_i <- target_log_dist(pr)
            if (m == 1 && i == 1) denominator_i <- target_log_dist(current_L)
            log_ratio_i <- numerator_i - denominator_i
            if (is.finite(log_ratio_i) && log_ratio_i > log(runif(1))) {
                current_L <- pr
                denominator_i <- numerator_i
                acceptance_L[m, i] <- 1
            }
            res_L[m, i] <- current_L[upper_tri_ind][i]
            if (m > 51) {
                scale_L[i] <- robbins_monro_univ(scale = scale_L_i,
                                                 acceptance_it = acceptance_L[m, i],
                                                 it = m - 50)
            }
        }
        #current_L[diags] <- sapply(split(current_L[upper_tri_ind], upper_tri_spl),
        #                           function (x) sqrt(1 - sum(x^2)))
    }
})

ar <- colMeans(acceptance_L[-seq_len(1000L), ])
ar

res_L <- res_L[-seq_len(1000L), ]
for (k in seq_len(K)) {
    plot(res_L[, k], type = "l", ylab = paste("Element:", k),
         main = ar[k])
}

cbind(round(colMeans(res_L), 3), round(L[upper_tri_ind], 3))

round(Reduce("+", res_R) / M, 3)
round(R, 3)

##########################################################################################
##########################################################################################

# sampling correlation matrices

p <- ncol(D)
sds <- sqrt(diag(D))
R <- cov2cor(D)
lambda_min <- min(eigen(R, symmetric = TRUE, only.values = TRUE)$values)
eps <- lambda_min / 2
K <- p


b <- MASS::mvrnorm(500, rep(0, p), D)

target_log_dist <- function (R) {
    D <- cor2cov(R, sds = sds)
    log_p_b <- sum(dmvnorm(b, rep(0, p), Sigma = D, log = TRUE, prop = FALSE))
    log_p_R <- as.vector(determinant(R)$modulus)
    log_p_b + log_p_R
}

M <- 6000L
acceptance_R <- numeric(M)
res_R <- keep <- matrix(0.0, M, p * (p - 1) / 2)
current_R <- R
system.time({
    for (m in seq_len(M)) {
        U <- matrix(rnorm(K * p), K, p)
        U <- U / rep(sqrt(colSums(U^2)), each = K)
        E <- crossprod(U); diag(E) <- 0.0
        keep[m, ] <- E[lower.tri(E)]
        proposed_R <- current_R + eps * E

        numerator <- target_log_dist(proposed_R)
        denominator <- target_log_dist(current_R)
        log_ratio <- numerator - denominator

        if (log_ratio > log(runif(1))) {
            current_R <- proposed_R
            acceptance_R[m] <- 1
        }
        res_R[m, ] <- current_R[lower.tri(current_R)]
        if (m > 20) {
            eps <- robbins_monro_univ(scale = eps, acceptance_it = acceptance_R[m],
                                      it = m, target_acceptance = 0.25)
        }
    }
})

mean(acceptance_R[-seq_len(1000L)])

res_R <- res_R[-seq_len(1000L), ]
plot(res_R[, 1], type = "l")
plot(res_R[, 2], type = "l")
plot(res_R[, 3], type = "l")
plot(res_R[, 4], type = "l")
plot(res_R[, 5], type = "l")
plot(res_R[, 6], type = "l")
plot(res_R[, 7], type = "l")
plot(res_R[, 8], type = "l")
plot(res_R[, 9], type = "l")
plot(res_R[, 10], type = "l")
plot(res_R[, 11], type = "l")
plot(res_R[, 12], type = "l")
plot(res_R[, 13], type = "l")
plot(res_R[, 14], type = "l")
plot(res_R[, 15], type = "l")
plot(res_R[, 16], type = "l")
plot(res_R[, 17], type = "l")
plot(res_R[, 18], type = "l")
plot(res_R[, 19], type = "l")
plot(res_R[, 20], type = "l")


cbind(colMeans(res_R), R[lower.tri(R)])



##########################################################################################
##########################################################################################

# sampling using the global variance, simplex weights approach

p <- ncol(D)
R <- cov2cor(D)
vars <- diag(D)
tau <- sum(vars) / p # the trace is p * tau
simplex <- vars / sum(vars)

D <- cor2cov(R, p * tau * simplex)

b <- MASS::mvrnorm(1000, rep(0, p), D)

target_log_dist <- function (tau, simplex) {
    p <- length(simplex)
    D <- cor2cov(R, p * tau * simplex)
    log_p_b <- sum(dmvnorm(b, rep(0, p), D, log = TRUE, prop = FALSE))
    log_p_tau <- dgamma(tau, 1, 1, log = TRUE)
    log_p_simplex <- ddirichlet(simplex, rep(1, p), log = TRUE)
    log_p_b + log_p_tau + log_p_simplex
}

M <- 3000
acceptance_tau <- taus <- numeric(M)
acceptance_simplex <- numeric(M)
simplexes <- matrix(0.0, M, p)
current_tau <- tau
current_simplex <- simplex
scale_tau <- 0.04
scale_simplex <- 2e4
#scale_tau <- 0.1
#scale_simplex <- 1e3
for (m in seq_len(M)) {
    log_mu <- log(current_tau) - 0.5 * scale_tau^2
    proposed_tau <- rlnorm(1, log_mu, scale_tau)
    numerator <- target_log_dist(proposed_tau, current_simplex) +
        dlnorm(current_tau, log_mu, scale_tau, log = TRUE)
    denominator <- target_log_dist(current_tau, current_simplex) +
        dlnorm(proposed_tau, log_mu, scale_tau, log = TRUE)
    log_ratio <- numerator - denominator
    if (log_ratio > log(runif(1))) {
        current_tau <- proposed_tau
        acceptance_tau[m] <- 1
    }
    taus[m] <- current_tau
    #######
    proposed_simplex <- c(rdirichlet(1, scale_simplex * current_simplex))

    numerator <- target_log_dist(current_tau, proposed_simplex) +
        ddirichlet(current_simplex, current_simplex, log = TRUE)

    denominator <- target_log_dist(current_tau, current_simplex) +
        ddirichlet(proposed_simplex, current_simplex, log = TRUE)

    log_ratio <- numerator - denominator
    if (log_ratio > log(runif(1))) {
        current_simplex <- proposed_simplex
        acceptance_simplex[m] <- 1
    }
    simplexes[m, ] <- current_simplex
    ####
    #scale_tau <- update_scale(scale_tau, mean(acceptance_tau), target_acc = 0.55, it = 3)
    #scale_simplex <- update_scale(scale_simplex, mean(acceptance_simplex),
    #                              target_acc = 0.33, it = 3, c1 = 0.8, c0 = -1)
    #print(c(scale_tau = scale_tau, scale_simplex = scale_simplex))
}

mean(acceptance_tau[-seq_len(500L)])
mean(acceptance_simplex[-seq_len(500L)])

####

plot(taus[-seq_len(500L)], type = "l")

simplexes <- simplexes[-seq_len(500L), ]
plot(simplexes[, 1], type = "l")
plot(simplexes[, 2], type = "l")
plot(simplexes[, 3], type = "l")
plot(simplexes[, 4], type = "l")
plot(simplexes[, 5], type = "l")
plot(simplexes[, 6], type = "l")

####

mean_tau <- mean(taus[-seq_len(500L)])
mean_simplex <- colMeans(simplexes)

cor2cov(R, p * mean_tau * mean_simplex)
D




