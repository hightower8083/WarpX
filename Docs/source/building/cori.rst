Building WarpX for Cori (NERSC)
===============================

Standard build
--------------

For the `Cori cluster
<http://www.nersc.gov/users/computational-systems/cori/>`__ at NERSC,
you need to type the following command when compiling:

.. note::

   In order to compile the code with a spectral solver, type

   ::

	module load cray-fftw

   before typing any of the commands below, and add ``USE_PSATD=TRUE``
   at the end of the command containing ``make``.

In order to compile for the **Haswell architecture**:

    * with the Intel compiler

    ::

        make -j 16 COMP=intel

    * with the GNU compiler

    ::

        module swap PrgEnv-intel PrgEnv-gnu
        make -j 16 COMP=gnu

In order to compile for the **Knight's Landing (KNL) architecture**:

    * with the Intel compiler

    ::

        module swap craype-haswell craype-mic-knl
        make -j 16 COMP=intel

    * with the GNU compiler

    ::

        module swap craype-haswell craype-mic-knl
        module swap PrgEnv-intel PrgEnv-gnu
        make -j 16 COMP=gnu

GPU Build
---------

To compile on the experimental GPU nodes on Cori, you first need to purge
your modules, most of which won't work on the GPU nodes.

   ::

	module purge

Then, you need to load the following modules:

    ::

        module load modules/3.2.10.6 esslurm cgpu/1.0 pgi/19.5 cuda/10.1 mpich/3.3-pgi-19.5 

Currently, you need to use OpenMPI; mvapich2 seems not to work.

Then, you need to use slurm to request access to a GPU node:

    ::

        salloc -C gpu -N 1 -t 30 -c 10 --gres=gpu:1 --mem=30GB -A m1759
       
This reserves 10 logical cores (5 physical), 1 GPU, and 30 GB of RAM for your job.
Note that you can't cross-compile for the GPU nodes - you have to log on to one
and then build your software.

Finally, navigate to the base of the WarpX repository and compile in GPU mode:

    ::

        make -j 16 COMP=pgi USE_GPU=TRUE


Building WarpX with openPMD support
-----------------------------------

First, load the appropriate modules:

::

    module swap craype-haswell craype-mic-knl
    module swap PrgEnv-intel PrgEnv-gnu
    module load cmake/3.14.4
    module load cray-hdf5-parallel
    module load adios/1.13.1 zlib
    export CRAYPE_LINK_TYPE=dynamic

Then, in the `warpx_directory`, download and build the openPMD API:

::

    git clone https://github.com/openPMD/openPMD-api.git
    mkdir openPMD-api-build
    cd openPMD-api-build
    cmake ../openPMD-api -DopenPMD_USE_PYTHON=OFF -DCMAKE_INSTALL_PREFIX=../openPMD-install/ -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=ON -DCMAKE_INSTALL_RPATH='$ORIGIN'
    cmake --build . --target install

Finally, compile WarpX:

::

    cd ../WarpX
    export PKG_CONFIG_PATH=$PWD/../openPMD-install/lib64/pkgconfig:$PKG_CONFIG_PATH
    make -j 16 COMP=gnu USE_OPENPMD=TRUE

In order to run WarpX, load the same modules again.
