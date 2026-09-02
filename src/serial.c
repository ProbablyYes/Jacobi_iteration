#include "jacobi.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static size_t cell(int row, int column, int width) {
    return (size_t)row * (size_t)width + (size_t)column;
}

int main(int argc, char **argv) {
    JacobiOptions options;
    JacobiResult result;
    char error[256];
    int parse_status;
    int width;
    size_t cells;
    double *old_grid;
    double *new_grid;
    double *rhs_grid;
    double h;
    double h2;
    double residual = 0.0;
    double compute_seconds = 0.0;
    int iterations = 0;
    FILE *trace = NULL;
    double started;
    double total_seconds;
    int row;
    int column;
    double error_sum = 0.0;
    double exact_sum = 0.0;
    double checksum = 0.0;
    int exit_code = 0;

    parse_status = jacobi_parse_options(argc, argv, &options, error, sizeof(error));
    if (parse_status == 1) {
        jacobi_print_usage(stdout, argv[0]);
        return 0;
    }
    if (parse_status != 0) {
        fprintf(stderr, "错误: %s\n", error);
        jacobi_print_usage(stderr, argv[0]);
        return 64;
    }

    if (options.trace_path != NULL) {
        trace = fopen(options.trace_path, "w");
        if (trace == NULL) {
            fprintf(stderr, "错误: 无法创建轨迹文件 %s\n", options.trace_path);
            return 64;
        }
        fputs("iteration,residual\n", trace);
    }

    width = options.size + 2;
    cells = (size_t)width * (size_t)width;
    if (cells > SIZE_MAX / sizeof(double)) {
        fprintf(stderr, "错误: 网格规模过大\n");
        if (trace != NULL) fclose(trace);
        return 64;
    }
    old_grid = calloc(cells, sizeof(double));
    new_grid = calloc(cells, sizeof(double));
    rhs_grid = calloc(cells, sizeof(double));
    if (old_grid == NULL || new_grid == NULL || rhs_grid == NULL) {
        fprintf(stderr, "错误: 无法为网格分配内存\n");
        free(old_grid);
        free(new_grid);
        free(rhs_grid);
        if (trace != NULL) fclose(trace);
        return 64;
    }

    h = 1.0 / (double)(options.size + 1);
    h2 = h * h;
    for (row = 1; row <= options.size; ++row) {
        double y = (double)row * h;
        for (column = 1; column <= options.size; ++column) {
            double x = (double)column * h;
            rhs_grid[cell(row, column, width)] = jacobi_rhs(x, y);
        }
    }
    started = jacobi_wall_seconds();

    for (;;) {
        double local_max = 0.0;
        double compute_started = jacobi_wall_seconds();

        for (row = 1; row <= options.size; ++row) {
            for (column = 1; column <= options.size; ++column) {
                size_t index = cell(row, column, width);
                double updated = 0.25 *
                    (old_grid[cell(row - 1, column, width)] +
                     old_grid[cell(row + 1, column, width)] +
                     old_grid[cell(row, column - 1, width)] +
                     old_grid[cell(row, column + 1, width)] +
                     h2 * rhs_grid[index]);
                double difference = fabs(updated - old_grid[index]);
                new_grid[index] = updated;
                if (difference > local_max) local_max = difference;
            }
        }
        compute_seconds += jacobi_wall_seconds() - compute_started;
        residual = 4.0 * local_max / h2;
        ++iterations;

        {
            double *temporary = old_grid;
            old_grid = new_grid;
            new_grid = temporary;
        }
        if (trace != NULL) fprintf(trace, "%d,%.17g\n", iterations, residual);

        if (options.fixed_iterations > 0) {
            if (iterations >= options.fixed_iterations) break;
        } else if (residual <= options.tolerance || iterations >= options.max_iterations) {
            break;
        }
    }
    total_seconds = jacobi_wall_seconds() - started;

    for (row = 1; row <= options.size; ++row) {
        double y = (double)row * h;
        for (column = 1; column <= options.size; ++column) {
            double exact = jacobi_exact((double)column * h, y);
            double value = old_grid[cell(row, column, width)];
            double difference = value - exact;
            error_sum += difference * difference;
            exact_sum += exact * exact;
            checksum += value;
        }
    }

    result.version = "serial";
    result.size = options.size;
    result.processes = 1;
    result.tolerance = options.tolerance;
    if (options.fixed_iterations > 0) {
        result.status = "fixed_iterations";
    } else if (residual <= options.tolerance) {
        result.status = "converged";
    } else {
        result.status = "max_iterations";
        exit_code = 2;
    }
    result.iterations = iterations;
    result.residual = residual;
    result.relative_l2_error = sqrt(error_sum / exact_sum);
    result.checksum = checksum;
    result.total_seconds = total_seconds;
    result.compute_seconds = compute_seconds;
    result.halo_issue_seconds = 0.0;
    result.halo_wait_seconds = 0.0;
    result.reduction_seconds = 0.0;
    jacobi_print_result(&result, options.format);

    free(old_grid);
    free(new_grid);
    free(rhs_grid);
    if (trace != NULL) fclose(trace);
    return exit_code;
}
