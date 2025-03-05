#include "../dep/laynii_lib.h"
#include <sstream>

int show_help(void) {
    printf(
    "LN2_SENSITIVITY: Compute a voxel-wise measure of functional sensitivity\n"
    "                 from a 4D matrix containing fMRI responses to N tasks\n"
    "                 (e.g., betas, percent signal change, t-statistics).\n"
    "                 This method provides a measure of how strongly a voxel responds\n" 
    "                 to different tasks (overall responsiveness of a voxel).\n"
    "\n"
    "Usage:\n"
    "    LN2_SENSITIVITY -input input.nii\n"
    "    ../LN2_SENSITIVITY -input input.nii\n"
    "\n"
    "Options:\n"
    "    -help   : Show this help.\n"
    "    -input  : 4D matrix of dimensions (X, Y, Z, N) where\n"
    "             (X, Y, Z) are the spatial dimensions of the brain volume\n"  
    "             and N is the number of task conditions (e.g. fMRI task responses)\n"
    "    -output : (Optional) Output basename for all outputs.\n"
    "\n"
    "Citation:\n"
    "    - Pizzuti, A., Huber, L., Gulban, O.F, Benitez-Andonegui A., Peters, J., Goebel R.,\n"
    "      (2023). Imaging the columnar functional organization of\n"
    "      human area MT+ to axis-of-motion stimuli using VASO at 7 Tesla.\n"
    "      Cerebral Cortex. <https://doi.org/10.1093/cercor/bhad151>\n"
    "\n"
    "NOTES: \n" 
    "    Sensitivity is based on the magnitude (ln2norm) of a voxel's response profile.\n"
    "    By default, negative values are zeroed before computation.\n"
 "\n");
    return 0;
}

int main(int argc, char*  argv[]) {
    nifti_image *nii1 = NULL;
    char *fin1 = NULL, *fin2 = NULL, *fout = NULL;
    int ac;
    bool mode_debug = false;

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

    log_welcome("LN2_SENSITIVITY - Ale WIP");
    log_nifti_descriptives(nii1);

    // Get dimensions of input
    const uint32_t size_x = nii1->nx;
    const uint32_t size_y = nii1->ny;
    const uint32_t size_z = nii1->nz;
    const uint32_t size_time = nii1->nt;

    const uint32_t nr_voxels = size_z * size_y * size_x;

    // ========================================================================
    // Fix input datatype issues and prepare 3D Nifti output
    // ========================================================================
    nifti_image* nii_input1 = copy_nifti_as_float32_with_scl_slope_and_scl_inter(nii1);
    float* nii_input1_data = static_cast<float*>(nii_input1->data);

    nifti_image* nii_sensitivity = copy_nifti_as_float32(nii_input1);  // Keep this copy
    float* nii_sensitivity_data = static_cast<float*>(nii_sensitivity->data);

    // Initialize output to zeros
    for (int i = 0; i != nr_voxels; ++i) {
        *(nii_sensitivity_data + i) = 0;
    }

    // Ensure it's correctly set as a 3D image
    nii_sensitivity->dim[0] = 3;
    nii_sensitivity->dim[1] = size_x;
    nii_sensitivity->dim[2] = size_y;
    nii_sensitivity->dim[3] = size_z;
    nii_sensitivity->dim[4] = 1;
    nifti_update_dims_from_array(nii_sensitivity);

    // // ========================================================================
    cout << " Calculating sensitivity..." << endl;
    // // ========================================================================
    // Compute L2 norm (Euclidean norm) across the time dimension

    for (uint32_t i = 0; i != nr_voxels; ++i) {  // Loop across voxels
        float sum_sq = 0;
        for (uint32_t t = 0; t != size_time; ++t) {  // Loop across time points 
            float val = *(nii_input1_data + i + nr_voxels*t);
            if (val < 0.0) {
                val = 0;
            }
            sum_sq += val * val;
        }
        // Store the computed L2 norm in the 3D output
        *(nii_sensitivity_data + i) = sqrt(sum_sq);
    }
    save_output_nifti(fout, "sensitivity", nii_sensitivity, true);

    cout << "\n  Finished." << endl;
    return 0;
}
