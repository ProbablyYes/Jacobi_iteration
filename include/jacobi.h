#ifndef JACOBI_H
#define JACOBI_H

#include <stddef.h>
#include <stdio.h>

typedef enum {
    OUTPUT_TEXT = 0,
    OUTPUT_CSV = 1
} OutputFormat;

typedef struct {
    int size;
    double tolerance;
    int max_iterations;
    int fixed_iterations;
    const char *trace_path;
    OutputFormat format;
} JacobiOptions;

typedef struct {
    const char *version;
    int size;
    int processes;
    double tolerance;
    const char *status;
    int iterations;
    double residual;
    double relative_l2_error;
    double checksum;
    double total_seconds;
    double compute_seconds;
    double halo_issue_seconds;
    double halo_wait_seconds;
    double reduction_seconds;
} JacobiResult;

void jacobi_default_options(JacobiOptions *options);
int jacobi_parse_options(int argc, char **argv, JacobiOptions *options,
                         char *error, size_t error_size);
void jacobi_print_usage(FILE *stream, const char *program);
void jacobi_print_result(const JacobiResult *result, OutputFormat format);

double jacobi_exact(double x, double y);
double jacobi_rhs(double x, double y);
double jacobi_wall_seconds(void);

#endif
