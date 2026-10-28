#!/usr/bin/env python3
"""Export paper figures from retained five-process summaries after timings finish."""
import csv
import os
from pathlib import Path
os.environ.setdefault('MPLCONFIGDIR','/tmp/platformator-convergence-matplotlib')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

HERE=Path(__file__).resolve().parent
plt.rcParams.update({'font.size':9,'svg.fonttype':'none','axes.spines.top':False,'axes.spines.right':False})

def load(suite):
    with (HERE/suite/'timing/summary.csv').open() as stream:
        return {(r['case_id'],r['workers'],r['broad_phase'],r['render_culling']):r for r in csv.DictReader(stream)}

def value(row,scope,stat='mean'):
    return float(row[f'scope.{scope}.avg.across_process_{stat}'])

def save(fig,name):
    fig.tight_layout()
    for extension in ['svg','png']:
        fig.savefig(HERE/(name+'.'+extension),dpi=180,bbox_inches='tight')
    plt.close(fig)

rows=load('replay')
fig,axes=plt.subplots(1,2,figsize=(9,3.3),sharey=True)
for ax,workers in zip(axes,['1','6']):
    for population,color in [(100,'#0072B2'),(1000,'#D55E00'),(10000,'#009E73')]:
        ratios=[];low=[];high=[]
        for pct in [0,1,10,100]:
            case=f'collision-n{population}-p{pct}'
            a=rows[(case,workers,'bvh','none')];b=rows[(case,workers,'segmented','none')]
            ratio=value(a,'spatial_replay')/value(b,'spatial_replay');ratios.append(ratio)
            low.append(ratio-value(a,'spatial_replay','min')/value(b,'spatial_replay','max'))
            high.append(value(a,'spatial_replay','max')/value(b,'spatial_replay','min')-ratio)
        ax.errorbar(range(4),ratios,yerr=[low,high],marker='o',capsize=2,color=color,label=f'N={population:,}')
    ax.axhline(1,color='#777777',linestyle='--',linewidth=.8)
    ax.set(yscale='log',xticks=range(4),xticklabels=['0','1','10','100'],xlabel='Changed bounds (%)',title=f'{workers} worker allowance')
axes[0].set_ylabel('BVH / segmented elapsed replay time')
axes[1].legend(frameon=False,loc="upper left",bbox_to_anchor=(1.02,1))
fig.suptitle('Collision replay: ratio of five process means; whiskers show observed process-mean ratio range')
save(fig,'collision-ratios')

fig,axes=plt.subplots(1,2,figsize=(9,3.3),sharey=True)
for ax,workers in zip(axes,['1','6']):
    for method,color in [('linear','#0072B2'),('bvh','#D55E00'),('segmented','#009E73')]:
        points=[];errors=[]
        for pct in [0,1,10,100]:
            case=f'visibility-n1000-change{pct}-'+('static' if pct==0 else 'moving')
            row=rows[(case,workers,'segmented',method)]
            points.append(value(row,'spatial_replay'));errors.append(value(row,'spatial_replay','stdev'))
        ax.errorbar(range(4),points,yerr=errors,marker='o',capsize=2,color=color,label=method)
    ax.set(yscale='log',xticks=range(4),xticklabels=['0','1','10','100'],xlabel='Changed bounds (%)',title=f'{workers} worker allowance')
axes[0].set_ylabel('Elapsed replay time (ms)')
axes[1].legend(frameon=False,loc="upper left",bbox_to_anchor=(1.02,1))
fig.suptitle('Visibility replay: N=1,000, 10% visible; means ± process standard deviation')
save(fig,'visibility-dynamic')

rows=load('engine')
fig,axes=plt.subplots(1,2,figsize=(9,3.3),sharey=True)
for ax,workers in zip(axes,['1','6']):
    for offset,(method,color) in enumerate([('none','#777777'),('linear','#0072B2'),('bvh','#D55E00'),('segmented','#009E73')]):
        selected=[rows[(f'engine-sprite-visible{pct}',workers,'segmented',method)] for pct in [0,1,10,100]]
        ax.bar([i+(offset-1.5)*.2 for i in range(4)],[value(r,'render') for r in selected],width=.19,
               yerr=[value(r,'render','stdev') for r in selected],capsize=2,color=color,label=method)
    ax.set(xticks=range(4),xticklabels=['0','1','10','100'],xlabel='Visible sprites (%)',title=f'{workers} worker allowance')
axes[0].set_ylabel('Actual software-render time (ms)')
axes[1].legend(frameon=False,loc="upper left",bbox_to_anchor=(1.02,1))
fig.suptitle('Actual SDL rendering: N=1,000; five-process means ± process standard deviation')
save(fig,'actual-render-fractions')
print('Exported three SVG and PNG figures. Error bars describe observed process spread, not confidence intervals.')
