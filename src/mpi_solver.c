#include "jacobi.h"

#include <math.h>
#include <mpi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef JACOBI_OVERLAP
#define JACOBI_OVERLAP 0
#endif

static size_t cell(int row, int column, int width) {
    return (size_t)row * (size_t)width + (size_t)column;
}

static double update_rows(const double *old_grid, double *new_grid,
                          const double *rhs_grid, int first_row, int last_row,
                          int size, int width, double h2) {
    double maximum = 0.0;
    int row;
    int column;

    for (row = first_row; row <= last_row; ++row) {
        for (column = 1; column <= size; ++column) {
            size_t index = cell(row, column, width);
            double updated = 0.25 *
                (old_grid[cell(row - 1, column, width)] +
                 old_grid[cell(row + 1, column, width)] +
                 old_grid[cell(row, column - 1, width)] +
                 old_grid[cell(row, column + 1, width)] +
                 h2 * rhs_grid[index]);
            double difference = fabs(updated - old_grid[index]);
            new_grid[index] = updated;
            if (difference > maximum) maximum = difference;
        }
    }
    return maximum;
}

int main(int argc, char **argv) {
    JacobiOptions options;
    JacobiResult result;
    char error[256];
    int parse_status;
    int rank;
    int processes;
    int local_rows;
    int global_first_row;
    int width;
    size_t cells;
    double *old_grid = NULL;
    double *new_grid = NULL;
    double *rhs_grid = NULL;
    double h;
    double h2;
    double residual = 0.0;
    int iterations = 0;
    double compute_seconds = 0.0;
    double halo_issue_seconds = 0.0;
    double halo_wait_seconds = 0.0;
    double reduction_seconds = 0.0;
    double started;
    double total_seconds;
    FILE *trace = NULL;
    int trace_ok = 1;
    int up;
    int down;
    int exit_code = 0;
    double local_error_sum = 0.0;
    double local_exact_sum = 0.0;
    double local_checksum = 0.0;
    double global_error_sum;
    double global_exact_sum;
    double global_checksum;
    double local_times[5];
    double maximum_times[5];
    int row;
    int column;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &processes);

    parse_status = jacobi_parse_options(argc, argv, &options, error, sizeof(error));
    if (parse_status == 1) {
        if (rank == 0) jacobi_print_usage(stdout, argv[0]);
        MPI_Finalize();
        return 0;
    }
    if (parse_status != 0) {
        if (rank == 0) {
            fprintf(stderr, "错误: %s\n", error);
            jacobi_print_usage(stderr, argv[0]);
        }
        MPI_Finalize();
        return 64;
    }
    if (processes > options.size) {
        if (rank == 0) fprintf(stderr, "错误: 进程数不能大于内部网格行数\n");
        MPI_Finalize();
        return 64;
    }

    if (rank == 0 && options.trace_path != NULL) {
        trace = fopen(options.trace_path, "w");
        if (trace == NULL) {
            fprintf(stderr, "错误: 无法创建轨迹文件 %s\n", options.trace_path);
            trace_ok = 0;
        } else {
            fputs("iteration,residual\n", trace);
        }
    }
    MPI_Bcast(&trace_ok, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (!trace_ok) {
        MPI_Finalize();
        return 64;
    }

    local_rows = options.size / processes + (rank < options.size % processes ? 1 : 0);
    global_first_row = rank * (options.size / processes) +
                       (rank < options.size % processes ? rank : options.size % processes) + 1;
    width = options.size + 2;
    cells = (size_t)(local_rows + 2) * (size_t)width;
    if (cells > SIZE_MAX / sizeof(double)) {
        if (rank == 0) fprintf(stderr, "错误: 网格规模过大\n");
        if (trace != NULL) fclose(trace);
        MPI_Finalize();
        return 64;
    }
    old_grid = calloc(cells, sizeof(double));
    new_grid = calloc(cells, sizeof(double));
    rhs_grid = calloc(cells, sizeof(double));
    if (old_grid == NULL || new_grid == NULL || rhs_grid == NULL) {
        fprintf(stderr, "rank %d: 无法为局部网格分配内存\n", rank);
        free(old_grid);
        free(new_grid);
        free(rhs_grid);
        if (trace != NULL) fclose(trace);
        MPI_Abort(MPI_COMM_WORLD, 64);
        return 64;
    }

    h = 1.0 / (double)(options.size + 1);
    h2 = h * h;
    for (row = 1; row <= local_rows; ++row) {
        int global_row = global_first_row + row - 1;
        double y = (double)global_row * h;
        for (column = 1; column <= options.size; ++column) {
            double x = (double)column * h;
            rhs_grid[cell(row, column, width)] = jacobi_rhs(x, y);
        }
    }
    up = rank == 0 ? MPI_PROC_NULL : rank - 1;
    down = rank == processes - 1 ? MPI_PROC_NULL : rank + 1;

    MPI_Barrier(MPI_COMM_WORLD);
    started = MPI_Wtime();

    for (;;) {
        double local_max = 0.0;
        double reduced_max;

#if JACOBI_OVERLAP
        MPI_Request requests[4];
        double section_started = MPI_Wtime();
        MPI_Irecv(&old_grid[cell(0, 0, width)], width, MPI_DOUBLE,
                  up, 101, MPI_COMM_WORLD, &requests[0]);
        MPI_Irecv(&old_grid[cell(local_rows + 1, 0, width)], width, MPI_DOUBLE,
                  down, 100, MPI_COMM_WORLD, &requests[1]);
        MPI_Isend(&old_grid[cell(1, 0, width)], width, MPI_DOUBLE,
                  up, 100, MPI_COMM_WORLD, &requests[2]);
        MPI_Isend(&old_grid[cell(local_rows, 0, width)], width, MPI_DOUBLE,
                  down, 101, MPI_COMM_WORLD, &requests[3]);
        halo_issue_seconds += MPI_Wtime() - section_started;

        section_started = MPI_Wtime();
        if (local_rows > 2) {
            local_max = update_rows(old_grid, new_grid, rhs_grid, 2, local_rows - 1,
                                    options.size, width, h2);
        }
        compute_seconds += MPI_Wtime() - section_started;

        section_started = MPI_Wtime();
        MPI_Waitall(4, requests, MPI_STATUSES_IGNORE);
        halo_wait_seconds += MPI_Wtime() - section_started;

        section_started = MPI_Wtime();
        reduced_max = update_rows(old_grid, new_grid, rhs_grid, 1, 1,
                                  options.size, width, h2);
        if (reduced_max > local_max) local_max = reduced_max;
        if (local_rows > 1) {
            reduced_max = update_rows(old_grid, new_grid, rhs_grid, local_rows, local_rows,
                                      options.size, width, h2);
            if (reduced_max > local_max) local_max = reduced_max;
        }
        compute_seconds += MPI_Wtime() - section_started;
#else
        double section_started = MPI_Wtime();
        MPI_Sendrecv(&old_grid[cell(1, 0, width)], width, MPI_DOUBLE, up, 100,
                     &old_grid[cell(local_rows + 1, 0, width)], width, MPI_DOUBLE,
                     down, 100, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Sendrecv(&old_grid[cell(local_rows, 0, width)], width, MPI_DOUBLE, down, 101,
                     &old_grid[cell(0, 0, width)], width, MPI_DOUBLE,
                     up, 101, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        halo_wait_seconds += MPI_Wtime() - section_started;

        section_started = MPI_Wtime();
        local_max = update_rows(old_grid, new_grid, rhs_grid, 1, local_rows,
                                options.size, width, h2);
        compute_seconds += MPI_Wtime() - section_started;
#endif

        {
            double reduction_started = MPI_Wtime();
            MPI_Allreduce(&local_max, &reduced_max, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
            reduction_seconds += MPI_Wtime() - reduction_started;
        }
        residual = 4.0 * reduced_max / h2;
        ++iterations;

        {
            double *temporary = old_grid;
            old_grid = new_grid;
            new_grid = temporary;
        }
        if (rank == 0 && trace != NULL) fprintf(trace, "%d,%.17g\n", iterations, residual);

        if (options.fixed_iterations > 0) {
            if (iterations >= options.fixed_iterations) break;
        } else if (residual <= options.tolerance || iterations >= options.max_iterations) {
            break;
        }
    }
    MPI_Barrier(MPI_COMM_WORLD);
    total_seconds = MPI_Wtime() - started;

    for (row = 1; row <= local_rows; ++row) {
        int global_row = global_first_row + row - 1;
        double y = (double)global_row * h;
        for (column = 1; column <= options.size; ++column) {
            double exact = jacobi_exact((double)column * h, y);
            double value = old_grid[cell(row, column, width)];
            double difference = value - exact;
            local_error_sum += difference * difference;
            local_exact_sum += exact * exact;
            local_checksum += value;
        }
    }
    MPI_Reduce(&local_error_sum, &global_error_sum, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_exact_sum, &global_exact_sum, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_checksum, &global_checksum, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

    local_times[0] = total_seconds;
    local_times[1] = compute_seconds;
    local_times[2] = halo_issue_seconds;
    local_times[3] = halo_wait_seconds;
    local_times[4] = reduction_seconds;
    MPI_Reduce(local_times, maximum_times, 5, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (options.fixed_iterations <= 0 && residual > options.tolerance) exit_code = 2;
    if (rank == 0) {
#if JACOBI_OVERLAP
        result.version = "mpi_overlap";
#else
        result.version = "mpi_blocking";
#endif
        result.size = options.size;
        result.processes = processes;
        result.tolerance = options.tolerance;
        result.status = options.fixed_iterations > 0 ? "fixed_iterations" :
                        (residual <= options.tolerance ? "converged" : "max_iterations");
        result.iterations = iterations;
        result.residual = residual;
        result.relative_l2_error = sqrt(global_error_sum / global_exact_sum);
        result.checksum = global_checksum;
        result.total_seconds = maximum_times[0];
        result.compute_seconds = maximum_times[1];
        result.halo_issue_seconds = maximum_times[2];
        result.halo_wait_seconds = maximum_times[3];
        result.reduction_seconds = maximum_times[4];
        jacobi_print_result(&result, options.format);
    }

    free(old_grid);
    free(new_grid);
    free(rhs_grid);
    if (trace != NULL) fclose(trace);
    MPI_Finalize();
    return exit_code;
}
