#!/bin/bash

run=0
noise=0.0
obj="random"
exp=5000000
reps=100
steps=100
version=3
outdir="figures"

dims=(2 4 8 16 32 64 128) 
pops=(8 16 32 64 128 256 512)

#dims=(2)
#pops=(8)

for i in "${!dims[@]}";
do

dim=${dims[i]}
pop=${pops[i]}

cat > run.sh <<EOF
#!/bin/bash -l
#SBATCH --job-name="RLCMAES_VRACER_$dim"
#SBATCH --output=RLCMAES_$dim_%j.out
#SBATCH --error=RLCMAES_$dim_err_%j.out
#SBATCH --time=12:00:00
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=12
#SBATCH --ntasks-per-core=1
#SBATCH --partition=normal
#SBATCH --constraint=gpu
#SBATCH --account=s929

RUNPATH=$SCRATCH/RLCMA2/\$SLURM_JOB_ID
mkdir -p \$RUNPATH

cat run-vracer.py

cp run-vracer.py \$RUNPATH
cp run-env-cmaes.py \$RUNPATH
cp read-history.py \$RUNPATH
cp -r _environment/ \$RUNPATH

pushd \$RUNPATH

mkdir -p ${outdir}

python run-vracer.py --noise $noise --obj $obj --dim $dim --pop $pop --run $run --exp $exp --steps $steps --version=$version 
python -m korali.rlview --dir "_vracer_${obj}_${dim}_${pop}_${noise}_${run}/" --out "${outdir}/${obj}_${dim}_${pop}_${run}.png"

python run-vracer.py --noise $noise --obj $obj --dim ${dim} --pop ${pop} --run $run --steps $steps --eval --reps $reps --version=$version

objectives=("fsphere" "felli" "fcigar" "ftablet" "fcigtab" "ftwoax" "fdiffpow" "rosenbrock" "fparabr" "fsharpr")
#objectives=("fsphere" "felli" "rosenbrock" "dixon" "ackley" "levi")

for o in "\${objectives[@]}";
do
    python run-env-cmaes.py --noise $noise --obj \$o --dim $dim --pop $pop --run $run --steps $steps --eval --reps $reps;
    vracerfile="history_vracer_\${o}_${dim}_${pop}_${noise}_${run}.npz"
    cmaesfile="history_cmaes_\${o}_${dim}_${pop}_${noise}_${run}.npz"
    outfile="${outdir}/history_\${o}_${dim}_${pop}_${noise}_${run}.png"
    python read-history.py --files \${vracerfile} \${cmaesfile} --out \${outfile}
done;


popd

date
EOF

sbatch run.sh

done;
