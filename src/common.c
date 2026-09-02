#define _POSIX_C_SOURCE 200809L

#include "jacobi.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int parse_positive_int(const char *text, int *value) {
    char *end = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed <= 0 || parsed > 2147483647L) {
        return -1;
    }
    *value = (int)parsed;
    return 0;
}

static int parse_positive_double(const char *text, double *value) {
    char *end = NULL;
    double parsed;

    errno = 0;
    parsed = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' || !isfinite(parsed) || parsed <= 0.0) {
        return -1;
    }
    *value = parsed;
    return 0;
}

void jacobi_default_options(JacobiOptions *options) {
    options->size = 64;
    options->tolerance = 1.0e-6;
    options->max_iterations = 1000000;
    options->fixed_iterations = 0;
    options->trace_path = NULL;
    options->format = OUTPUT_TEXT;
}

int jacobi_parse_options(int argc, char **argv, JacobiOptions *options,
                         char *error, size_t error_size) {
    int i;

    jacobi_default_options(options);
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            return 1;
        }
        if (i + 1 >= argc) {
            snprintf(error, error_size, "参数 %s 缺少取值", argv[i]);
            return -1;
        }
        if (strcmp(argv[i], "--size") == 0) {
            if (parse_positive_int(argv[++i], &options->size) != 0) {
                snprintf(error, error_size, "--size 必须是正整数");
                return -1;
            }
        } else if (strcmp(argv[i], "--tol") == 0) {
            if (parse_positive_double(argv[++i], &options->tolerance) != 0) {
                snprintf(error, error_size, "--tol 必须是有限正数");
                return -1;
            }
        } else if (strcmp(argv[i], "--max-iters") == 0) {
            if (parse_positive_int(argv[++i], &options->max_iterations) != 0) {
                snprintf(error, error_size, "--max-iters 必须是正整数");
                return -1;
            }
        } else if (strcmp(argv[i], "--fixed-iters") == 0) {
            if (parse_positive_int(argv[++i], &options->fixed_iterations) != 0) {
                snprintf(error, error_size, "--fixed-iters 必须是正整数");
                return -1;
            }
        } else if (strcmp(argv[i], "--trace") == 0) {
            options->trace_path = argv[++i];
        } else if (strcmp(argv[i], "--format") == 0) {
            const char *format = argv[++i];
            if (strcmp(format, "text") == 0) {
                options->format = OUTPUT_TEXT;
            } else if (strcmp(format, "csv") == 0) {
                options->format = OUTPUT_CSV;
            } else {
                snprintf(error, error_size, "--format 只能是 text 或 csv");
                return -1;
            }
        } else {
            snprintf(error, error_size, "未知参数: %s", argv[i]);
            return -1;
        }
    }
    if (options->size > INT_MAX - 2) {
        snprintf(error, error_size, "--size 过大");
        return -1;
    }
    return 0;
}

void jacobi_print_usage(FILE *stream, const char *program) {
    fprintf(stream,
            "用法: %s [选项]\n"
            "  --size N          每个方向的内部网格点数（默认 64）\n"
            "  --tol EPS         全局无穷范数残差阈值（默认 1e-6）\n"
            "  --max-iters K     最大迭代次数（默认 1000000）\n"
            "  --fixed-iters K   固定运行 K 轮，忽略提前收敛\n"
            "  --trace FILE      写出 iteration,residual 收敛轨迹\n"
            "  --format FMT      text 或 csv（默认 text）\n"
            "  --help            显示本帮助\n",
            program);
}

void jacobi_print_result(const JacobiResult *result, OutputFormat format) {
    if (format == OUTPUT_CSV) {
        puts("version,size,processes,tolerance,status,iterations,residual,relative_l2_error,checksum,total_seconds,compute_seconds,halo_issue_seconds,halo_wait_seconds,reduction_seconds");
        printf("%s,%d,%d,%.17g,%s,%d,%.17g,%.17g,%.17g,%.9f,%.9f,%.9f,%.9f,%.9f\n",
               result->version, result->size, result->processes, result->tolerance,
               result->status, result->iterations, result->residual,
               result->relative_l2_error, result->checksum, result->total_seconds,
               result->compute_seconds, result->halo_issue_seconds,
               result->halo_wait_seconds, result->reduction_seconds);
        return;
    }

    printf("Jacobi Parallel Experiment\n");
    printf("  Version              %s\n", result->version);
    printf("  Grid                  %d x %d interior points\n", result->size, result->size);
    printf("  Processes             %d\n", result->processes);
    printf("  Status                %s\n", result->status);
    printf("  Iterations            %d\n", result->iterations);
    printf("  Final residual        %.8e\n", result->residual);
    printf("  Relative L2 error     %.8e\n", result->relative_l2_error);
    printf("  Solution checksum     %.12e\n", result->checksum);
    printf("  Total                 %.6f s\n", result->total_seconds);
    printf("  Computation           %.6f s\n", result->compute_seconds);
    printf("  Halo issue            %.6f s\n", result->halo_issue_seconds);
    printf("  Exposed halo wait     %.6f s\n", result->halo_wait_seconds);
    printf("  Global reduction      %.6f s\n", result->reduction_seconds);
}

double jacobi_exact(double x, double y) {
    const double pi = acos(-1.0);
    return sin(pi * x) * sin(pi * y);
}

double jacobi_rhs(double x, double y) {
    const double pi = acos(-1.0);
    return 2.0 * pi * pi * jacobi_exact(x, y);
}

double jacobi_wall_seconds(void) {
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + 1.0e-9 * (double)now.tv_nsec;
}
