#include "../dep/laynii_lib.h"
#include <sstream>

int show_help(void) {
    printf(
    "LN3_NOLAD: Nonlinear anisotropic diffusion filter.\n"
    "\n"
    "Usage:\n"
    "    LN3_NOLAD -input input.nii\n"
    "    ../LN3_NOLAD -input input.nii\n"
    "\n"
    "Options:\n"
    "    -help    : Show this help.\n"
    "    -input   : Nifti image that will be used to compute gradients.\n"
    "               This can be a 4D nifti. in 4D case, 3D gradients\n"
    "               will be computed for each volume.\n"
    "    -nr_iter : (Optional) Number of iterations.\n"
    "    -nscale  : (Optional) Noise scale. Number of Gaussian smoothing iterations applied \n"
    "               to scalar image. No smoothing ('0') by default.\n"
    "    -fscale  : (Optional) Feature scale. Number of Gaussian smoothing iterations applied \n"
    "               to first order gradients (vector field). No smoothing ('0') by default.\n"
    "    -output  : (Optional) Output basename for all outputs.\n"
    "    -debug   : (Optional) Save extra intermediate outputs.\n"
    "\n"
    "\n");
    return 0;
}

// NOTE[Faruk]: References are:
// - Weickert, J. (1998). Anisotropic diffusion in image processing. Image Rochester NY, 256(3), 170.
// - Mirebeau, J.-M., Fehrenbach, J., Risser, L., & Tobji, S. (2015). Anisotropic Diffusion in ITK, 1-9.

int main(int argc, char*  argv[]) {
    nifti_image *nii1 = NULL;
    char *fin1 = NULL, *fout = NULL;
    int ac;
    bool mode_debug = false;
    int NSCALE = 0, FSCALE = 0, NR_ITER = 5;
    float LAMBDA=0.001, ALPHA=0.001, M=4;


    // Process user options
    if (argc < 2) return show_help();
    for (ac = 1; ac < argc; ac++) {
        if (!strncmp(argv[ac], "-h", 2)) {
            return show_help();
        } else if (!strcmp(argv[ac], "-input")) {
            if (++ac >= argc) {
                fprintf(stderr, "** missing argument for -input\n");
                return 1;
            }
            fin1 = argv[ac];
            fout = argv[ac];
        } else if (!strcmp(argv[ac], "-nr_iter")) {
            if (++ac >= argc) {
                fprintf(stderr, "** missing argument for -nr_iter\n");
            } else {
                NR_ITER = atof(argv[ac]);
            }
        } else if (!strcmp(argv[ac], "-nscale")) {
            if (++ac >= argc) {
                fprintf(stderr, "** missing argument for -nscale\n");
            } else {
                NSCALE = atof(argv[ac]);
            }
        } else if (!strcmp(argv[ac], "-fscale")) {
            if (++ac >= argc) {
                fprintf(stderr, "** missing argument for -fscale\n");
            } else {
                FSCALE = atof(argv[ac]);
            }
        } else if (!strcmp(argv[ac], "-debug")) {
            mode_debug = true;
        } else if (!strcmp(argv[ac], "-output")) {
            if (++ac >= argc) {
                fprintf(stderr, "** missing argument for -output\n");
                return 1;
            }
            fout = argv[ac];
        } else {
            fprintf(stderr, "** invalid option, '%s'\n", argv[ac]);
            return 1;
        }
    }

    if (!fin1) {
        fprintf(stderr, "** missing option '-input'\n");
        return 1;
    }

    // Read input dataset, including data
    nii1 = nifti_image_read(fin1, 1);
    if (!nii1) {
        fprintf(stderr, "** failed to read NIfTI from '%s'\n", fin1);
        return 2;
    }

    log_welcome("LN3_NOLAD");
    log_nifti_descriptives(nii1);

    // Get dimensions of input
    const uint32_t nx = nii1->nx;
    const uint32_t ny = nii1->ny;
    const uint32_t nz = nii1->nz;
    const uint32_t nt = nii1->nt;

    const uint32_t end_x = nx - 1;
    const uint32_t end_y = ny - 1;
    const uint32_t end_z = nz - 1;

    const uint32_t nr_voxels = nz * ny * nx;
    const uint32_t data_size = nz * ny * nx * nt;

    const float dx = nii1->pixdim[1];
    const float dy = nii1->pixdim[2];
    const float dz = nii1->pixdim[3];

    // ========================================================================
    // Fix input datatype issues
    // ========================================================================
    nifti_image* nii_input = copy_nifti_as_float32(nii1);
    float* data_input = static_cast<float*>(nii_input->data);

    // Prepare generic output niftis
    nifti_image* nii_out_float32 = copy_nifti_as_float32(nii1);
    float* nii_out_float32_data = static_cast<float*>(nii_out_float32->data);

    // ========================================================================
    // Normalize by maximum
    // ========================================================================
    std::printf("\n  Normalizing (minimum to 0 and maximum to 1)...\n");
    ln_normalize_to_zero_one(data_input, data_size);

    save_output_nifti(fout, "normalized_to_zero_one", nii_input, true);        

    for (uint32_t ii = 0; ii != NR_ITER; ++ii) {
        std::printf("  Iteration: %d\n", ii+1);

        // ========================================================================
        // Noise scale smoothing
        // ========================================================================
        float FWHM = 0.5;  // Default
        if (NSCALE > 0) {
            std::printf("\n  Smoothing (iterative 3D Gaussian [FWHM = %f, iterations = %i])...\n", FWHM, NSCALE);
            ln_smooth_gaussian_iterative_3D(data_input, nx, ny, nz, nt, dx, dy, dz, FWHM, NSCALE);

            if (mode_debug) {
                std::printf("  DEBUG: Saving output...\n");
                save_output_nifti(fout, "DEBUG2-smooth_gaussian", nii_input, true);            
            }
        }

        // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        // TODO: This gradient computation here is unnecesary if I retain the gradient where it is first computed above.
        float* data_gra1 = (float*)malloc(data_size * sizeof(float));
        float* data_gra2 = (float*)malloc(data_size * sizeof(float));
        float* data_gra3 = (float*)malloc(data_size * sizeof(float));
        ln_compute_gradients_3D(data_input, data_gra1, data_gra2, data_gra3, nx, ny, nz, nt);
        ln_smooth_gaussian_iterative_3D(data_gra1, nx, ny, nz, nt, dx, dy, dz, FWHM, FSCALE);
        ln_smooth_gaussian_iterative_3D(data_gra2, nx, ny, nz, nt, dx, dy, dz, FWHM, FSCALE);
        ln_smooth_gaussian_iterative_3D(data_gra3, nx, ny, nz, nt, dx, dy, dz, FWHM, FSCALE);
        // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

        
        // ========================================================================
        // Compute Hessian
        // ========================================================================
        std::printf("\n  Computing Hessian matrices...\n");

        float* data_hessian  = (float*)malloc(data_size * 6 * sizeof(float));
        ln_compute_hessian_3D(data_input, data_hessian, nx, ny, nz, nt, dx, dy, dz, FSCALE);
        
        // ------------------------------------------------------------------------
        if (mode_debug) {
            // Compute trace (for testing)
            float* data_trace = (float*)malloc(data_size * sizeof(float));
            for (uint32_t i = 0; i != data_size; ++i) {
                *(data_trace + i) = *(data_hessian + i*6 + 0) + *(data_hessian + i*6 + 3) + *(data_hessian + i*6 + 5);
            }
            std::printf("  DEBUG: Saving output...\n");
            for (uint32_t i = 0; i != data_size; ++i) *(nii_out_float32_data + i) = *(data_trace + i);
            free(data_trace);
            save_output_nifti(fout, "DEBUG3-hessian_trace", nii_out_float32, true);

            // Compute off-trace (for testing)
            float* data_offtrace = (float*)malloc(data_size * sizeof(float));
            for (uint32_t i = 0; i != data_size; ++i) {
                *(data_offtrace + i) = *(data_hessian + i*6 + 1) + *(data_hessian + i*6 + 2) + *(data_hessian + i*6 + 4);
            }
            std::printf("  DEBUG: Saving output...\n");
            for (uint32_t i = 0; i != data_size; ++i) *(nii_out_float32_data + i) = *(data_offtrace + i);
            free(data_offtrace);
            save_output_nifti(fout, "DEBUG3-hessian_offtrace", nii_out_float32, true);  
        }

        // ========================================================================
        // Compute Eigen values
        // ========================================================================
        std::printf("\n  Computing Eigen values...\n");

        float* data_eigval1 = (float*)malloc(data_size * sizeof(float));
        float* data_eigval2 = (float*)malloc(data_size * sizeof(float));
        float* data_eigval3 = (float*)malloc(data_size * sizeof(float));
        ln_compute_eigen_values_3D(data_hessian, data_eigval1, data_eigval2, data_eigval3, nx, ny, nz, nt);

        // ------------------------------------------------------------------------
        if (mode_debug) {
            std::printf("  DEBUG: Saving output...\n"); 
            for (uint32_t i = 0; i != data_size; ++i) *(nii_out_float32_data + i) = *(data_eigval1 + i);
            save_output_nifti(fout, "DEBUG4-eigen_value_1", nii_out_float32, true);
            for (uint32_t i = 0; i != data_size; ++i) *(nii_out_float32_data + i) = *(data_eigval2 + i);
            save_output_nifti(fout, "DEBUG4-eigen_value_2", nii_out_float32, true);
            for (uint32_t i = 0; i != data_size; ++i) *(nii_out_float32_data + i) = *(data_eigval3 + i);
            save_output_nifti(fout, "DEBUG4-eigen_value_3", nii_out_float32, true);        
        }

        // ========================================================================
        // Compute diffusion weights
        // ========================================================================
        std::printf("\n  Computing diffusion weights...\n");

        float* data_diffw1 = (float*)malloc(data_size * sizeof(float));
        float* data_diffw2 = (float*)malloc(data_size * sizeof(float));
        float* data_diffw3 = (float*)malloc(data_size * sizeof(float));

        for (uint32_t i = 0; i != data_size; ++i) {
            // Preserve signs
            float sign1 = std::copysignf(1.0f, *(data_eigval1 + i));
            float sign2 = std::copysignf(1.0f, *(data_eigval2 + i)); 
            float sign3 = std::copysignf(1.0f, *(data_eigval3 + i));
            
            // Apply compositional closure
            float eigvalsum = std::abs(*(data_eigval1 + i)) + std::abs(*(data_eigval2 + i)) + std::abs(*(data_eigval3 + i));
            if (eigvalsum == 0) {
                *(data_diffw1 + i) = 0;
                *(data_diffw2 + i) = 0;
                *(data_diffw3 + i) = 0;
            } else {
                *(data_diffw1 + i) =  1 - ( std::abs(*(data_eigval1 + i)) / eigvalsum );
                *(data_diffw2 + i) =  1 - ( std::abs(*(data_eigval2 + i)) / eigvalsum );
                *(data_diffw3 + i) =  1 - ( std::abs(*(data_eigval3 + i)) / eigvalsum );

                // Reclose for balance
                eigvalsum = *(data_diffw1 + i) + *(data_diffw2 + i) + *(data_diffw3 + i);
                *(data_diffw1 + i) /= eigvalsum;
                *(data_diffw2 + i) /= eigvalsum;
                *(data_diffw3 + i) /= eigvalsum;

                // Reassign signs
                *(data_diffw1 + i) *= sign1;
                *(data_diffw2 + i) *= sign2;
                *(data_diffw3 + i) *= sign3;
            }
        }

        // -------------------------------------------------------------------------
        if (mode_debug) {
            std::printf("  DEBUG: Saving output...\n"); 
            for (uint32_t i = 0; i != data_size; ++i) *(nii_out_float32_data + i) = *(data_diffw1 + i);
            save_output_nifti(fout, "DEBUG5-diffweight_1", nii_out_float32, true);
            for (uint32_t i = 0; i != data_size; ++i) *(nii_out_float32_data + i) = *(data_diffw2 + i);
            save_output_nifti(fout, "DEBUG5-diffweight_2", nii_out_float32, true);
            for (uint32_t i = 0; i != data_size; ++i) *(nii_out_float32_data + i) = *(data_diffw3 + i);
            save_output_nifti(fout, "DEBUG5-diffweight_3", nii_out_float32, true);        
        }

        // ========================================================================
        // Construct diffusion tensor with direct update
        // ========================================================================
        std::printf("\n  Constructing diffusion tensors...\n");
        ln_update_shorthessian(data_hessian, data_diffw1, data_diffw2, data_diffw3, nx, ny, nz, nt);

        // ------------------------------------------------------------------------
        if (mode_debug) {
            // Compute trace (for testing)
            float* data_trace = (float*)malloc(data_size * sizeof(float));
            for (uint32_t i = 0; i != data_size; ++i) {
                *(data_trace + i) = *(data_hessian + i*6 + 0) + *(data_hessian + i*6 + 3) + *(data_hessian + i*6 + 5);
            }
            std::printf("  DEBUG: Saving output...\n");
            for (uint32_t i = 0; i != data_size; ++i) *(nii_out_float32_data + i) = *(data_trace + i);
            free(data_trace);
            save_output_nifti(fout, "DEBUG6-hessian_trace", nii_out_float32, true);

            // Compute off-trace (for testing)
            float* data_offtrace = (float*)malloc(data_size * sizeof(float));
            for (uint32_t i = 0; i != data_size; ++i) {
                *(data_offtrace + i) = *(data_hessian + i*6 + 1) + *(data_hessian + i*6 + 2) + *(data_hessian + i*6 + 4);
            }
            std::printf("  DEBUG: Saving output...\n");
            for (uint32_t i = 0; i != data_size; ++i) *(nii_out_float32_data + i) = *(data_offtrace + i);
            free(data_offtrace);
            save_output_nifti(fout, "DEBUG6-hessian_offtrace", nii_out_float32, true);  

        }

        // ========================================================================
        // Compute negative flux field
        // ========================================================================
        
        // Compute dot_product_matrix_vector. Weickert, 1998, eq. 1.1 (Fick's law). Yields vector fields.
        ln_multiply_matrix_vector_3D(data_hessian, data_gra1, data_gra2, data_gra3, nx, ny, nz, nt);

        // Compute divergence. Weickert, 1998, eq. 1.2 (continuity equation). Yields scalar field.
        float* data_diffusion_difference = (float*)malloc(data_size * sizeof(float));
        ln_compute_divergence_3D(data_diffusion_difference, data_gra1, data_gra2, data_gra3, nx, ny, nz, nt);

        // Update image (diffuse image using the difference)
        float GAMMA = 0.25;
        for (uint32_t i = 0; i != data_size; ++i) {
            *(data_input + i) += *(data_diffusion_difference + i) * GAMMA;
        }
    }
    save_output_nifti(fout, "TEST-FINAL", nii_input, true);  

    cout << "\n  Finished." << endl;
    return 0;
}
