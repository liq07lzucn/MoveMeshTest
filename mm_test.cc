#include <deal.II/base/logstream.h>
#include <deal.II/base/mpi.h>
#include <deal.II/base/conditional_ostream.h>

#include <deal.II/distributed/tria.h>
#include <deal.II/grid/tria.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria_accessor.h>
#include <deal.II/grid/tria_iterator.h>
#include <deal.II/distributed/solution_transfer.h>
#include <deal.II/base/conditional_ostream.h>
#include <deal.II/base/utilities.h>

#include <deal.II/dofs/dof_handler.h>

#include <deal.II/fe/fe_q.h>
#include <deal.II/fe/fe_system.h>

#include <deal.II/lac/trilinos_vector.h>
#include <deal.II/lac/constraint_matrix.h>

#include <iostream>
#include <stdlib.h>
#include <time.h>

#include "myheaders/mesh_struct.h"

using namespace dealii;

template <int dim>
class mm_test{
public:
    mm_test();
    ~mm_test();

    void run();

private:

    MPI_Comm                                  	mpi_communicator;
    parallel::distributed::Triangulation<dim> 	triangulation;
    DoFHandler<dim>                             mesh_dof_handler;
    FESystem<dim>                              	mesh_fe;
    TrilinosWrappers::MPI::Vector               mesh_vertices;
    TrilinosWrappers::MPI::Vector               distributed_mesh_vertices;
    IndexSet                                    mesh_locally_owned;
    IndexSet                                    mesh_locally_relevant;
    ConstraintMatrix                            mesh_constraints;
    ConditionalOStream                        	pcout;

    Mesh_struct<dim>                            mesh_struct;

    void make_grid();
    void refine_transfer(std::string prefix);


};

template <int dim>
mm_test<dim>::mm_test()
    :
    mpi_communicator (MPI_COMM_WORLD),
    triangulation (mpi_communicator,
                    typename Triangulation<dim>::MeshSmoothing
                    (Triangulation<dim>::smoothing_on_refinement |
                    Triangulation<dim>::limit_level_difference_at_vertices)),
    mesh_dof_handler (triangulation),
    mesh_fe (FE_Q<dim>(1),dim),
    mesh_struct(0.1,0.01),
    pcout(std::cout,(Utilities::MPI::this_mpi_process(mpi_communicator) == 0))
{
    make_grid();
}

template <int dim>
mm_test<dim>::~mm_test(){
    mesh_dof_handler.clear();
}

template <int dim>
void mm_test<dim>::make_grid(){
    Point<dim> left_bottom;
    Point<dim> right_top;
    std::vector<unsigned int>	n_cells;
    if (dim == 2) {
        right_top[0] = 5000; right_top[1] = 300;
        n_cells.push_back(20); n_cells.push_back(5);
    }
    else if (dim == 3){
        right_top[0] = 5000; right_top[1] = 5000; right_top[2] = 300;
        n_cells.push_back(10); n_cells.push_back(10); n_cells.push_back(3);
    }

    GridGenerator::subdivided_hyper_rectangle(triangulation,
                                                      n_cells,
                                                      left_bottom,
                                                      right_top,
                                                      true);
}

template <int dim>
void mm_test<dim>::refine_transfer(std::string prefix){
    unsigned int my_rank = Utilities::MPI::this_mpi_process(mpi_communicator);
    // first prepare the triangulation
    triangulation.prepare_coarsening_and_refinement();

    //prepare vertices for transfering
    parallel::distributed::SolutionTransfer<dim, TrilinosWrappers::MPI::Vector>mesh_trans(mesh_dof_handler);
    std::vector<const TrilinosWrappers::MPI::Vector *> x_fs_system (1);

    x_fs_system[0] = &mesh_vertices;
    mesh_trans.prepare_for_coarsening_and_refinement(x_fs_system);

    std::cout << "Number of active cells Before: "
                << triangulation.n_active_cells()
                << std::endl;
    pcout << "dofs 2" << mesh_dof_handler.n_dofs() << std::endl << std::flush;
    // execute the actual refinement
    triangulation.execute_coarsening_and_refinement ();

    std::cout << "Number of active cells After: "
                << triangulation.n_active_cells()
                << std::endl;
    pcout << "dofs 3" << mesh_dof_handler.n_dofs() << std::endl << std::flush;

    //For the mesh
    mesh_dof_handler.distribute_dofs(mesh_fe); // distribute the dofs again
    pcout << "dofs 4" << mesh_dof_handler.n_dofs() << std::endl << std::flush;
    mesh_locally_owned = mesh_dof_handler.locally_owned_dofs();
    DoFTools::extract_locally_relevant_dofs (mesh_dof_handler, mesh_locally_relevant);

    distributed_mesh_vertices.reinit(mesh_locally_owned, mpi_communicator);
    distributed_mesh_vertices.compress(VectorOperation::insert);

    std::vector<TrilinosWrappers::MPI::Vector *> mesh_tmp (1);
    mesh_tmp[0] = &(distributed_mesh_vertices);

    mesh_trans.interpolate (mesh_tmp);
    mesh_vertices.reinit (mesh_locally_owned, mesh_locally_relevant, mpi_communicator);
    mesh_vertices = distributed_mesh_vertices;

    pcout << "moving vertices " << std::endl << std::flush;
    mesh_struct.move_vertices(mesh_dof_handler,
                              mesh_vertices,
                              my_rank, prefix);


}

template <int dim>
void mm_test<dim>::run(){
    unsigned int my_rank = Utilities::MPI::this_mpi_process(mpi_communicator);

    // after we generated the mesh we update the our custom Mesh structure
    mesh_struct.updateMeshStruct(mesh_dof_handler,
                                 mesh_fe,
                                 mesh_constraints,
                                 mesh_locally_owned,
                                 mesh_locally_relevant,
                                 mesh_vertices,
                                 distributed_mesh_vertices,
                                 mpi_communicator,
                                 pcout,
                                 "iter0");



    // Set Top and Bottom elevation
    RBF rbf;
    std::vector<double> cntrs, wdth;
    cntrs.push_back(1000); wdth.push_back(0.001);
    cntrs.push_back(2000); wdth.push_back(0.001);
    cntrs.push_back(3000); wdth.push_back(0.001);
    cntrs.push_back(4000);wdth.push_back(0.001);
    rbf.assign_centers(cntrs,wdth);
    rbf.assign_weights(mpi_communicator);

    // Set initial top bottom elebation elevation
    typename std::map<int , PntsInfo<dim> >::iterator it;
    for (it = mesh_struct.PointsMap.begin(); it != mesh_struct.PointsMap.end(); ++it){
        it->second.T = 300;// this is supposed to set the initial elevation
        it->second.B = 0;
        // Here we update the top
        it->second.T += rbf.eval(it->second.PNT[0]);
    }
    //std::cout << "I'm rank: " << my_rank << " V(20)= " << rbf.eval(20) << std::endl;
    // THe structure is used to update the elevation
    mesh_struct.updateMeshElevation(mesh_dof_handler,
                                    mesh_constraints,
                                    mesh_vertices,
                                    distributed_mesh_vertices,
                                    mpi_communicator,
                                    pcout,
                                    "iter0");





    // refine the updated elevations
    typename parallel::distributed::Triangulation<dim>::active_cell_iterator
    cell = triangulation.begin_active(),
    endc = triangulation.end();
    for (; cell!=endc; ++cell){
        if (cell->is_locally_owned()){
            int r = rand() % 100 + 1;
            if (r < 30)
                cell->set_refine_flag ();
            else if(r > 95)
                cell->set_coarsen_flag();
        }
    }
    // The refine transfer refines and updates the triangulation and mesh_dof_handler
    refine_transfer("refine0");


    // Then we need to update the custon mesh structure after any change of the triangulation
    mesh_struct.updateMeshStruct(mesh_dof_handler,
                                 mesh_fe,
                                 mesh_constraints,
                                 mesh_locally_owned,
                                 mesh_locally_relevant,
                                 mesh_vertices,
                                 distributed_mesh_vertices,
                                 mpi_communicator,
                                 pcout, "iter1");

    std::cout << "------------------------------------------------------------" << std::endl;

    // modify top function
    rbf.centers.push_back(500); rbf.width.push_back(0.002);
    rbf.centers.push_back(1500); rbf.width.push_back(0.002);
    rbf.centers.push_back(2500); rbf.width.push_back(0.002);
    rbf.centers.push_back(3500); rbf.width.push_back(0.002);
    rbf.centers.push_back(4500); rbf.width.push_back(0.002);
    rbf.assign_weights(mpi_communicator);



    for (it = mesh_struct.PointsMap.begin(); it != mesh_struct.PointsMap.end(); ++it){
        it->second.B = 0;
        it->second.T = 300;
        it->second.T += rbf.eval(it->second.PNT[0]);
    }
    //std::cout << "I'm rank: " << my_rank << " V(20)= " << rbf.eval(20) << std::endl;

    mesh_struct.updateMeshElevation(mesh_dof_handler,
                                    mesh_constraints,
                                    mesh_vertices,
                                    distributed_mesh_vertices,
                                    mpi_communicator,
                                    pcout,"iter1");


    // ------------------ Second refinment iteration ---------------------------------------------------

    for (int i = 0; i < 3; ++i){
        pcout << "====================== ITER: " << i << "=============================" << std::endl;
        // refine the updated elevations
        cell = triangulation.begin_active(),
        endc = triangulation.end();
        for (; cell!=endc; ++cell){
            if (cell->is_locally_owned()){
                int r = rand() % 100 + 1;
                if (r < 20)
                    cell->set_refine_flag();
                else if(r > 95)
                    cell->set_coarsen_flag();
            }
        }
        // The refine transfer refines and updates the triangulation and mesh_dof_handler
        refine_transfer("refine" + std::to_string(i+1));

        //if (i == 1)
        //    return;


        // Then we need to update the custon mesh structure after any change of the triangulation
        mesh_struct.updateMeshStruct(mesh_dof_handler,
                                     mesh_fe,
                                     mesh_constraints,
                                     mesh_locally_owned,
                                     mesh_locally_relevant,
                                     mesh_vertices,
                                     distributed_mesh_vertices,
                                     mpi_communicator,
                                     pcout, "iter" + std::to_string(i+2));



        rbf.assign_weights(mpi_communicator);

        for (it = mesh_struct.PointsMap.begin(); it != mesh_struct.PointsMap.end(); ++it){
            it->second.B = 0;
            it->second.T = 300;
            it->second.T += rbf.eval(it->second.PNT[0]);
        }
        //std::cout << "I'm rank: " << my_rank << " V(20)= " << rbf.eval(20) << std::endl;

        mesh_struct.updateMeshElevation(mesh_dof_handler,
                                        mesh_constraints,
                                        mesh_vertices,
                                        distributed_mesh_vertices,
                                        mpi_communicator,
                                        pcout, "iter" + std::to_string(i+2));
        return;

    }





}

int main (int argc, char **argv){
    deallog.depth_console (1);

    //srand (time(NULL));
    int rr = time(NULL);
    std::cout << rr << std::endl;
    srand(rr);
    Utilities::MPI::MPI_InitFinalize mpi_initialization(argc, argv, 1);

    mm_test<2> mm;
    mm.run();


    return 0;
}
