#include "../dep/laynii_lib.h"


int show_help(void) {
    printf(
    "LN2_GEODISTANCE: Measure geodesic distances from a set of voxels.\n"
    "\n"
    "Usage:\n"
    "    LN2_GEODISTANCE -domain mask.nii -init points.nii \n"
    "\n"
    "Options:\n"
    "    -help      : Show this help.\n"
    "    -init      : Initial voxels that denote 0 distance.\n"
    "    -domain    : Set of voxels in which the distance will be measured.\n"
    "                 All non-zero voxels will be considered.\n"
    "    -max_dist  : (Optional) Maximum distance that will be computed.\n"
    "    -init_val  : (Optional) Initial voxels will be determined by this value.\n"
    "                  This is useful when the domian and init files are the same\n"
    "                  file, but the user wants to only take e.g. all values that\n"
    "                  are '2' within the domain file.\n"
    "    -no_smooth : (Optional) Disable smoothing on distance metric.\n"
    "    -output    : (Optional) Output basename for all outputs.\n"
    "\n"
    "\n");
    return 0;
}

int main(int argc, char*  argv[]) {

    nifti_image *nii1 = NULL, *nii2 = NULL;
    char *fin1 = NULL, *fin2 = NULL, *fout = NULL;
    bool use_outpath = false, mode_smooth = true, mode_init_val = false, mode_max_dist = false;
    int ac;
    float max_dist = std::numeric_limits<float>::max();
    float temp_max_dist = 0;
    int init_val;

    // Process user options
    if (argc < 2) return show_help();
    for (ac = 1; ac < argc; ac++) {
        if (!strncmp(argv[ac], "-h", 2)) {
            return show_help();
        } else if (!strcmp(argv[ac], "-init")) {
            if (++ac >= argc) {
                fprintf(stderr, "** missing argument for -init\n");
                return 1;
            }
            fin1 = argv[ac];
            fout = argv[ac];
        } else if (!strcmp(argv[ac], "-domain")) {
            if (++ac >= argc) {
                fprintf(stderr, "** missing argument for -domain\n");
                return 1;
            }
            fin2 = argv[ac];
        } else if (!strcmp(argv[ac], "-max_dist")) {
            if (++ac >= argc) {
                fprintf(stderr, "** missing argument for -max_dist\n");
                return 1;
            }
            mode_max_dist = true;
            max_dist = atof(argv[ac]);
        } else if (!strcmp(argv[ac], "-init_val")) {
            if (++ac >= argc) {
                fprintf(stderr, "** missing argument for -init_val\n");
                return 1;
            }
            mode_init_val = true;
            init_val = atof(argv[ac]);
        } else if (!strcmp(argv[ac], "-output")) {
            if (++ac >= argc) {
                fprintf(stderr, "** missing argument for -output\n");
                return 1;
            }
            fout = argv[ac];
            use_outpath = true;
        } else if (!strcmp(argv[ac], "-no_smooth")) {
            mode_smooth = false;
        } else {
            fprintf(stderr, "** invalid option, '%s'\n", argv[ac]);
            return 1;
        }
    }

    if (!fin1) {
        fprintf(stderr, "** missing option '-init'\n");
        return 1;
    }
    if (!fin2) {
        fprintf(stderr, "** missing option '-domain'\n");
        return 1;
    }

    // Read input dataset, including data
    nii1 = nifti_image_read(fin1, 1);
    if (!nii1) {
        fprintf(stderr, "** failed to read NIfTI from '%s'\n", fin1);
        return 2;
    }
    nii2 = nifti_image_read(fin2, 1);
    if (!nii2) {
        fprintf(stderr, "** failed to read NIfTI from '%s'\n", fin2);
        return 2;
    }

    log_welcome("LN2_GEODISTANCE");
    log_nifti_descriptives(nii1);
    log_nifti_descriptives(nii2);

    // Get dimensions of input
    const uint32_t size_x = nii1->nx;
    const uint32_t size_y = nii1->ny;
    const uint32_t size_z = nii1->nz;

    const uint32_t end_x = size_x - 1;
    const uint32_t end_y = size_y - 1;
    const uint32_t end_z = size_z - 1;

    const uint32_t nr_voxels = size_z * size_y * size_x;

    const float dX = nii1->pixdim[1];
    const float dY = nii1->pixdim[2];
    const float dZ = nii1->pixdim[3];

    // Short diagonals
    const float dia_xy = sqrt(dX * dX + dY * dY);
    const float dia_xz = sqrt(dX * dX + dZ * dZ);
    const float dia_yz = sqrt(dY * dY + dZ * dZ);
    // Long diagonals
    const float dia_xyz = sqrt(dX * dX + dY * dY + dZ * dZ);

    // ========================================================================
    // Fix input datatype issues
    nifti_image* nii_init = copy_nifti_as_int32(nii1);
    int32_t* nii_init_data = static_cast<int32_t*>(nii_init->data);
    nifti_image* nii_domain = copy_nifti_as_int32(nii2);
    int32_t* nii_domain_data = static_cast<int32_t*>(nii_domain->data);

    // Prepare flood fill related nifti images
    nifti_image* flood_step = copy_nifti_as_int32(nii_init);
    int32_t* flood_step_data = static_cast<int32_t*>(flood_step->data);
    nifti_image* flood_dist = copy_nifti_as_float32(nii_init);
    float* flood_dist_data = static_cast<float*>(flood_dist->data);

    // Setting zero
    for (uint32_t i = 0; i != nr_voxels; ++i) {
        *(flood_step_data + i) = 0;
        *(flood_dist_data + i) = 0;
    }

    // ------------------------------------------------------------------------
    // NOTE(Faruk): This section is written to constrain the big iterative
    // flooding distance loop to the subset of voxels. Required for substantial
    // speed boost.
    // ------------------------------------------------------------------------
    // Find the subset voxels that will be used many times
    uint32_t nr_voi = 0;  // Voxels of interest
    for (uint32_t i = 0; i != nr_voxels; ++i) {
        if (*(nii_domain_data + i) > 0){
            nr_voi += 1;
        }
    }
    cout << "  Domain voxels = " << nr_voi << endl;

    // Allocate memory to only the voxel of interest
    int32_t* voi_id;
    voi_id = (int32_t*) malloc(nr_voi*sizeof(int32_t));
    // Fill in indices to be able to remap from subset to full set of voxels
    uint32_t ii = 0;
    for (uint32_t i = 0; i != nr_voxels; ++i) {
        if (*(nii_domain_data + i) > 0){
            *(voi_id + ii) = i;
            ii += 1;
        }
    }

    // Handle initial voxels file
    uint32_t nr_init_voxels = 0;
    for (uint32_t i = 0; i != nr_voxels; ++i) {
        if (mode_init_val) {
            if (*(nii_init_data + i) != init_val) {
                *(nii_init_data + i) = 0;
            } else {
                *(nii_init_data + i) = 1;
                nr_init_voxels += 1;
            }
        } else {
            if (*(nii_init_data + i) != 0) {
                nr_init_voxels += 1;
            }
        }
    }

    if (mode_init_val) {
        cout << "  Initial voxels (custom inital voxels mode) = " << nr_init_voxels << endl;
    } else {
        cout << "  Initial voxels = " << nr_init_voxels << endl;
    }

    if (mode_max_dist) {
        cout << "  Maximum distance mode selected." << endl;
        cout << "    Maximum distance = " << max_dist << endl;
    }

    // ========================================================================
    // Borders
    // ========================================================================
    cout << "\n  Finding geodesic distances..." << endl;

    int32_t grow_step = 1;
    uint32_t voxel_counter = nr_voxels;
    uint32_t ix, iy, iz, i, j;
    float d;

    // TODO(Faruk): Guesstimate an initial distance to axis lines. Probably
    // I can do this better by considering the local neighbourhood in the
    // future.
    float dist_to_axes = ((dX + dY + dZ) / 3) / 2;  // Half a voxel

    // Initialize grow volume
    for (uint32_t i = 0; i != nr_voxels; ++i) {
        if (*(nii_init_data + i) != 0) {
            *(flood_step_data + i) = 1.;
            *(flood_dist_data + i) = dist_to_axes;
        }
    }

    while (voxel_counter != 0 && temp_max_dist < max_dist) {
        voxel_counter = 0;
        for (uint32_t ii = 0; ii != nr_voi; ++ii) {
            i = *(voi_id + ii);  // Map subset to full set
            if (*(flood_step_data + i) == grow_step) {
                tie(ix, iy, iz) = ind2sub_3D(i, size_x, size_y);
                voxel_counter += 1;

                // --------------------------------------------------------
                // 1-jump neighbours
                // --------------------------------------------------------
                if (ix > 0) {
                    j = sub2ind_3D(ix-1, iy, iz, size_x, size_y);
                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dX;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }
                if (ix < end_x) {
                    j = sub2ind_3D(ix+1, iy, iz, size_x, size_y);
                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dX;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }
                if (iy > 0) {
                    j = sub2ind_3D(ix, iy-1, iz, size_x, size_y);
                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dY;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }
                if (iy < end_y) {
                    j = sub2ind_3D(ix, iy+1, iz, size_x, size_y);
                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dY;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }
                if (iz > 0) {
                    j = sub2ind_3D(ix, iy, iz-1, size_x, size_y);
                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dZ;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }
                if (iz < end_z) {
                    j = sub2ind_3D(ix, iy, iz+1, size_x, size_y);

                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dZ;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }
                // --------------------------------------------------------
                // 2-jump neighbours
                // --------------------------------------------------------
                if (ix > 0 && iy > 0) {
                    j = sub2ind_3D(ix-1, iy-1, iz, size_x, size_y);

                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dia_xy;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }
                if (ix > 0 && iy < end_y) {
                    j = sub2ind_3D(ix-1, iy+1, iz, size_x, size_y);

                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dia_xy;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }
                if (ix < end_x && iy > 0) {
                    j = sub2ind_3D(ix+1, iy-1, iz, size_x, size_y);

                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dia_xy;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }
                if (ix < end_x && iy < end_y) {
                    j = sub2ind_3D(ix+1, iy+1, iz, size_x, size_y);

                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dia_xy;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }
                if (iy > 0 && iz > 0) {
                    j = sub2ind_3D(ix, iy-1, iz-1, size_x, size_y);

                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dia_yz;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }
                if (iy > 0 && iz < end_z) {
                    j = sub2ind_3D(ix, iy-1, iz+1, size_x, size_y);

                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dia_yz;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }
                if (iy < end_y && iz > 0) {
                    j = sub2ind_3D(ix, iy+1, iz-1, size_x, size_y);

                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dia_yz;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }
                if (iy < end_y && iz < end_z) {
                    j = sub2ind_3D(ix, iy+1, iz+1, size_x, size_y);

                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dia_yz;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }
                if (ix > 0 && iz > 0) {
                    j = sub2ind_3D(ix-1, iy, iz-1, size_x, size_y);

                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dia_xz;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }
                if (ix < end_x && iz > 0) {
                    j = sub2ind_3D(ix+1, iy, iz-1, size_x, size_y);

                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dia_xz;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }
                if (ix > 0 && iz < end_z) {
                    j = sub2ind_3D(ix-1, iy, iz+1, size_x, size_y);

                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dia_xz;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }
                if (ix < end_x && iz < end_z) {
                    j = sub2ind_3D(ix+1, iy, iz+1, size_x, size_y);

                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dia_xz;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }

                // --------------------------------------------------------
                // 3-jump neighbours
                // --------------------------------------------------------
                if (ix > 0 && iy > 0 && iz > 0) {
                    j = sub2ind_3D(ix-1, iy-1, iz-1, size_x, size_y);

                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dia_xyz;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }
                if (ix > 0 && iy > 0 && iz < end_z) {
                    j = sub2ind_3D(ix-1, iy-1, iz+1, size_x, size_y);

                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dia_xyz;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }
                if (ix > 0 && iy < end_y && iz > 0) {
                    j = sub2ind_3D(ix-1, iy+1, iz-1, size_x, size_y);

                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dia_xyz;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }
                if (ix < end_x && iy > 0 && iz > 0) {
                    j = sub2ind_3D(ix+1, iy-1, iz-1, size_x, size_y);

                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dia_xyz;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }
                if (ix > 0 && iy < end_y && iz < end_z) {
                    j = sub2ind_3D(ix-1, iy+1, iz+1, size_x, size_y);

                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dia_xyz;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }
                if (ix < end_x && iy > 0 && iz < end_z) {
                    j = sub2ind_3D(ix+1, iy-1, iz+1, size_x, size_y);

                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dia_xyz;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }
                if (ix < end_x && iy < end_y && iz > 0) {
                    j = sub2ind_3D(ix+1, iy+1, iz-1, size_x, size_y);

                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dia_xyz;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }
                if (ix < end_x && iy < end_y && iz < end_z) {
                    j = sub2ind_3D(ix+1, iy+1, iz+1, size_x, size_y);

                    if (*(nii_domain_data + j) > 0) {
                        d = *(flood_dist_data + i) + dia_xyz;
                        if (d < *(flood_dist_data + j)
                            || *(flood_dist_data + j) == 0) {
                            *(flood_dist_data + j) = d;
                            *(flood_step_data + j) = grow_step + 1;
                        }
                    }
                }

                // Update maximum distance reached
                if (*(flood_dist_data + i) > temp_max_dist) {
                    temp_max_dist = *(flood_dist_data + i);
                }
            }
        }
        grow_step += 1;
    }

    if (mode_max_dist) {
        cout << "\n  Maximum distance mode disables smoothing. Distance maps will not be smoothed... " << endl;
        // NOTE[Faruk]: I could not think of a way to not taper the max edges in max distance case. Therefore,
        // I have put this workaround for now.
        mode_smooth = false;
    }

    if (mode_smooth) {
        cout << "\n  Start mildly smoothing distances..." << endl;
        flood_dist = iterative_smoothing(flood_dist, 3, nii_domain, 1);
    }

    save_output_nifti(fout, "geodistance", flood_dist, true, use_outpath);

    cout << "\n  Finished." << endl;
    return 0;
}
