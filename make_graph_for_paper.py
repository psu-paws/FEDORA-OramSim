import pandas as pd

import matplotlib.pyplot as plt
import numpy as np

import math

LIFE_TIME_DRIVE_WRITES = 5437

SYSTEM_NAME = "FEDORA"

GREY_BAR_COLOR = "#808080"

patterns = ["\\"] * 2 + ["-" , "/" , "+" , "x", "o",] + [" "]
colors = [GREY_BAR_COLOR,] * 2 +  ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', '#9467bd', '#8c564b', '#e377c2']

# DATASETS = ("movielens_padded", "movielens_unpadded", "taobao_padded", "taobao_unpadded")
DATASETS = ("kaggle", "taobao_unpadded", "movielens_unpadded",  "movielens_padded", "taobao_padded",)
DISPLAY_NAMES = (
    "Kaggle",
    "Taobao (Hide priv val)",
    "Movielens (Hide priv val)",
    "Movielens (Hide # of priv val)",
    "Taobao (Hide # of priv val)",
)

BAR_OFFSETS = (
    0,
    1.5,
    3,
    4,
    5,
    6,
    7,
    8
)


PLOT_HEIGHT = 2.0
PLOT_WIDTH = 13.0
PLOT_TOP_ADJ = 0.69
PLOT_SPACE_ADJ = 0.15

X_LABELS = ("PathORAM+", "FEDORA(ε=0)", "FEDORA(ε=1)",)
X_LABEL_OFFSETS = (0, 1.5, 5.5)
X_LABEL_FONTSIZE = 9

UPDATES_PER_ROUND_TO_BUFFER_MODE = {
    10_000: "LinearScannedPosMap",
    100_000: "PosMap",
    1_000_000: "PosMap"
}

def geomean(iterable) -> float:
    a = np.array(iterable)
    return a.prod() ** (1.0 / len(a))

def main():
    table_sizes = ("Small", "Medium", "Large")
    
    df = pd.read_csv("recsys_sim_results.csv", skipinitialspace=True)
    
    df.insert(df.shape[1], "Rounds per Hour", 60 * 60 / df["Time per Round"])
    
    df.insert(df.shape[1], "Lifetime Rounds", df["Main Tree Size"] * LIFE_TIME_DRIVE_WRITES / df["Total Bytes Wrote"] * df["Rounds"])
    
    df.insert(df.shape[1], "Lifetime Years", ((df["Time per Round"] / 60) + 2) * df["Lifetime Rounds"] / 60 / 24/ 365.25)
    
    # df.insert(df.shape[1], "SSD life time (num accesses)", LIFE_TIME_DRIVE_WRITES * df["main_tree_size"] / df["bytes wrote per access"])
    
    df.to_csv("recsys_sim_results_processed.csv")
    
    # names = ["Path ORAM+",
    #         "FEDORA No Buf.",
    #         "FEDORA No Pop.",
    #         "FEDORA Pop-25%",
    #         "FEDORA Pop-Kaggle",
    #         "FEDORA Pop-75%"]
    names = (
        "PathORAM",
        "Strawman",
        # "StrawmanUnsafe",
    ) + DISPLAY_NAMES + (
        "Geomean",
    )
    buffer_performance = {k: [] for k in names}

    lines = []

    x = np.arange(len(table_sizes))  # the label locations
    width = 1 / (len(buffer_performance) + 2)  # the width of the bars
    center = 7.5 / 2 * width
    
    
    SAMPLE_SIZE_TEXTS = ("10K", "100K", "1M")
    
    plt.cla()
    fig, axs = plt.subplots(1, 3)
    # fig.set_figheight(PLOT_HEIGHT * 0.675)
    fig.set_figheight(PLOT_HEIGHT)
    fig.set_figwidth(PLOT_WIDTH) 
    for index, samples_per_round in enumerate((10_000, 100_000, 1_000_000)):
        for k in buffer_performance:
            buffer_performance[k] = list()
        
        sample_group = df[df["Samples per Round"] == samples_per_round]
        buffer_config = UPDATES_PER_ROUND_TO_BUFFER_MODE[samples_per_round]
        print(sample_group)
        for oram_config in table_sizes:
            oram_config_group = sample_group[sample_group["ORAM Config"] == oram_config]
            print(oram_config_group["Type"])
            poraw_group = oram_config_group[oram_config_group["Type"] == "PageOptimizedRAWOram"]
            print(poraw_group)
            # for table_buffer_config in ("Strawman", ): # ("StrawmanSafe", #"StrawmanUnsafe"):
            config = poraw_group[poraw_group["Buffer Config"] == "StrawmanSafe"]
                # print(config)
            buffer_performance["Strawman"].append(config["Time per Round"].min() / 120)
            
            geomean_buffer = []

            for display_name, dataset in zip(DISPLAY_NAMES, DATASETS):
                dataset_selection_mask = (poraw_group["Dataset"] == dataset) & (poraw_group["Buffer Config"] == buffer_config)
                dataset_group = poraw_group[dataset_selection_mask]
                print(dataset_group)
                
                buffer_performance[display_name].append(dataset_group["Time per Round"].min() / 120)
                geomean_buffer.append(dataset_group["Time per Round"].min())
            
            config = oram_config_group[oram_config_group["Type"] == "BinaryPathOram2"]
            print(config)
            buffer_performance["PathORAM"].append(config["Time per Round"].min() / 120)

            geomean_value = geomean(geomean_buffer)
            buffer_performance["Geomean"].append(geomean_value / 120)
        
        ax = axs[index]
        legend_handels = []
        for bar_index, (attribute, measurement) in enumerate(buffer_performance.items()):
            multiplier = BAR_OFFSETS[bar_index]
            
            offset = width * multiplier

            rects = ax.bar(x + offset, measurement, width, label=attribute, hatch=patterns[bar_index] * 3, alpha=0.99, color=colors[bar_index])
            if attribute in DISPLAY_NAMES or bar_index == 0 or bar_index == len(buffer_performance) - 1:
                legend_handels.append(rects)
            # ax.bar_label(rects, padding=3

            for size_index, value in enumerate(measurement):
                lines.append((samples_per_round, table_sizes[size_index], attribute, value))
        

        
        if index == 0:
            ax.set_ylabel('Round Overhead\nw.r.t 2-minute FL round')
        #ax.set_title(f"")
        #if index == 2:

        # # enable minor ticks only on the y axes
        # ax.yaxis.get_ticklocs(minor=True)
        
        # ax.minorticks_on()
        # ax.xaxis.set_tick_params(which='minor', bottom=False)

        myticklabels = X_LABELS * 3
        xtick_locations = []
        for i in range(3):
            for offset in X_LABEL_OFFSETS:
                xtick_locations.append(offset * width + i)
        ax.set_xticks(xtick_locations)
        ax.set_xticklabels(myticklabels, rotation=45, ha="right", fontsize=X_LABEL_FONTSIZE)
        # ax.set_xticks(x + width * (len(buffer_performance) - 1) / 2, table_sizes)
        ax.set_xlabel(f"{SAMPLE_SIZE_TEXTS[index]} Updates Per Round")

        # do the large medium small labeling
        level2_label_offset = center
        for i, label in enumerate(("Small", "Medium", "Large")):
            ax.text(i + level2_label_offset, 1, label, ha="center", va="bottom", clip_on=False,  transform=ax.get_xaxis_transform(), fontsize=X_LABEL_FONTSIZE)

        if index == 0 :
            #fig.subplots_adjust(top=0.87)
            fig.legend(handles=legend_handels, labels=("All", ) + DISPLAY_NAMES + ("Geomean",) ,loc="upper center", ncol=math.ceil((len(DISPLAY_NAMES) + 2) / 2))
            
    # fig.subplots_adjust(wspace=PLOT_SPACE_ADJ)
    fig.subplots_adjust(top=PLOT_TOP_ADJ, wspace=PLOT_SPACE_ADJ)
    plt.savefig(f"Recsys_latency.pdf", bbox_inches="tight")

    lines.sort()

    with open("Recsys_latency_data.csv", "w") as outfile:
        outfile.write("Samples per Round, ORAM Size, Label, Overhead fraction compared to 2 min \n")
        for row in lines:
            outfile.write(", ".join(map(str, row)) + "\n")


    # return
    
    buffer_performance = {k: [] for k in names}

    x = np.arange(len(table_sizes))  # the label locations
    # width = 1 / (len(buffer_performance) + 1)  # the width of the bars
    
    
    SAMPLE_SIZE_TEXTS = ("10K", "100K", "1M")
    
    plt.cla()
    fig, axs = plt.subplots(1, 3)
    fig.set_figheight(PLOT_HEIGHT)
    fig.set_figwidth(PLOT_WIDTH) 

    lines = []

    for index, samples_per_round in enumerate((10_000, 100_000, 1_000_000)):
        for k in buffer_performance:
            buffer_performance[k] = list()
        
        sample_group = df[df["Samples per Round"] == samples_per_round]
        print(sample_group)
        for oram_config in table_sizes:
            oram_config_group = sample_group[sample_group["ORAM Config"] == oram_config]
            print(oram_config_group["Type"])
            poraw_group = oram_config_group[oram_config_group["Type"] == "PageOptimizedRAWOram"]
            print(poraw_group)
            #for table_buffer_config in ("StrawmanSafe", "StrawmanUnsafe"):
            config = poraw_group[poraw_group["Buffer Config"] == "StrawmanSafe"]
                # print(config)
            buffer_performance["Strawman"].append(config["Lifetime Years"].max() * 12)
            
            geomean_buffer = []
            for display_name, dataset in zip(DISPLAY_NAMES, DATASETS):
                dataset_selection_mask = (poraw_group["Dataset"] == dataset) & (poraw_group["Buffer Config"] == buffer_config)
                dataset_group = poraw_group[dataset_selection_mask]
                print(dataset_group)
                
                buffer_performance[display_name].append(dataset_group["Lifetime Years"].max() * 12)
                geomean_buffer.append(dataset_group["Lifetime Years"].max())
            
            config = oram_config_group[oram_config_group["Type"] == "BinaryPathOram2"]
            print(config)
            buffer_performance["PathORAM"].append(config["Lifetime Years"].max() * 12)
            geomean_value = geomean(geomean_buffer)
            buffer_performance["Geomean"].append(geomean_value * 12)
        
        ax = axs[index]
        legend_handels = []
        for bar_index, (attribute, measurement) in enumerate(buffer_performance.items()):
            multiplier = BAR_OFFSETS[bar_index]
            
            offset = width * multiplier

            rects = ax.bar(x + offset, measurement, width, label=attribute, hatch=patterns[bar_index] * 3, alpha=0.99, color=colors[bar_index])
            if attribute in DISPLAY_NAMES or bar_index == 0 or bar_index == len(buffer_performance) - 1:
                legend_handels.append(rects)
            
            for size_index, value in enumerate(measurement):
                lines.append((samples_per_round, table_sizes[size_index], attribute, value))
        
        #if index == 0:
        ax.axhline(y=60, linestyle = 'dashed', color="blue")
        ax.axhline(y=24, linestyle = ':', color = "blue")
        if index == 0:
            ax.set_ylabel('Lifetime (Months)')
        # ax.set_ylim([0, 10])
        ax.set_yscale("log")

        myticklabels = X_LABELS * 3
        xtick_locations = []
        for i in range(3):
            for offset in X_LABEL_OFFSETS:
                xtick_locations.append(offset * width + i)
        ax.set_xticks(xtick_locations)
        ax.set_xticklabels(myticklabels, rotation=45, ha="right", fontsize=X_LABEL_FONTSIZE)
        # ax.set_xticks(x + width * (len(buffer_performance) - 1) / 2, table_sizes)
        ax.set_xlabel(f"{SAMPLE_SIZE_TEXTS[index]} Updates Per Round")
       
        # do the large medium small labeling
        level2_label_offset = center
        for i, label in enumerate(("Small", "Medium", "Large")):
            ax.text(i + level2_label_offset, 1, label, ha="center", va="bottom", clip_on=False,  transform=ax.get_xaxis_transform(), fontsize=X_LABEL_FONTSIZE)

        if index == 0 :
            #fig.subplots_adjust(top=0.87)
            fig.legend(handles=legend_handels, labels=("All", ) + DISPLAY_NAMES + ("Geomean",) ,loc="upper center", ncol=math.ceil((len(DISPLAY_NAMES) + 2) / 2))
        
        if index == 2:
            ax.text(0, 60, "5 years", color = "b", verticalalignment="bottom")
            ax.text(0, 24, "2 years", color = "b", verticalalignment="top")
            
    fig.subplots_adjust(top=PLOT_TOP_ADJ, wspace=PLOT_SPACE_ADJ)
    
    plt.savefig(f"Recsys_lifespan_months.pdf", bbox_inches="tight")

    lines.sort()

    with open("Recsys_lifespan_months_data.csv", "w") as outfile:
        outfile.write("Samples per Round, ORAM Size, Label, Lifespan Months \n")
        for row in lines:
            outfile.write(", ".join(map(str, row)) + "\n")

    
if __name__ == "__main__":
    main()