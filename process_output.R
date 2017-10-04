
library(reshape2)
library(ggplot2)
#library(svglite)
library(stringi)
library(plyr)
options(scipen=1)

df <- read.table("multi.normal.out",header=TRUE)


etchr <- subset(df,app == 1 & subpolicy == "normal" & w_hit_rate != -1, select=c(accesses,w_hit_rate) )
psahr <- subset(df,app == 2 & subpolicy == "normal" & w_hit_rate != -1, select=c(accesses,w_hit_rate) )


hrlist <- list()
hrlist[["etc"]] = etchr
hrlist[["psa"]] = psahr


etcbytes <- subset(df,app == 1 & subpolicy == "normal", select=c(accesses,bytes_in_use) )
psabytes <- subset(df,app == 2 & subpolicy == "normal" , select=c(accesses,bytes_in_use) )

byteslist <- list()
byteslist[["etc"]] = etcbytes
byteslist[["psa"]] = psabytes

etcshq <- subset(df,app == 1  & subpolicy == "normal", select=c(accesses,shadow_q_hits) )
psashq <- subset(df,app == 2  & subpolicy == "normal", select=c(accesses,shadow_q_hits) )

shqlist <- list()
shqlist[["etc"]] = etcshq
shqlist[["psa"]] = psashq

etctmem <- subset(df,app == 1  & subpolicy == "normal", select=c(accesses,share) )
psatmem <- subset(df,app == 2  & subpolicy == "normal", select=c(accesses,share) )

etctmem$total = etctmem$share 
psatmem$total = psatmem$share

etctmem <- subset(etctmem, select=c(accesses,total) )
psatmem <- subset(psatmem, select=c(accesses,total) )

tmemlist <- list()
tmemlist[["etc"]] = etctmem
tmemlist[["psa"]] = psatmem

hrc_df = melt(hrlist, id="accesses")
#rename(hrc_df,c("L1"="System"))

ggplot(data=hrc_df, aes(x=accesses, y=value, colour=L1) ) +
    geom_line() +
    theme_bw() +
    theme(legend.position="bottom", legend.box = "horizontal", aspect.ratio=1) +
    expand_limits(y=0) +
    labs(title = "Hit Rate vs. accesses", 
         y = "Hit Rate", 
         x = "accesses (logical access)",
         colour = "" ) 

shq_df = melt(shqlist, id="accesses")
#rename(hrc_df,c("L1"="System"))

ggplot(data=shq_df, aes(x=accesses, y=value, colour=L1) ) +
    geom_line() +
    theme_bw() +
    theme(legend.position="bottom", legend.box = "horizontal", aspect.ratio=1) +
    expand_limits(y=0) +
    labs(title = "Shadow Queue Hits vs. accesses", 
         y = "Shadow Queue Hits", 
         x = "accesses (logical access)",
         colour = "" ) 

#bytes_df = melt(byteslist, id="accesses")
##rename(hrc_df,c("L1"="System"))
#
#ggplot(data=bytes_df, aes(x=accesses, y=value, colour=L1) ) +
#    geom_line() +
#    theme_bw() +
#    theme(legend.position="bottom", legend.box = "horizontal", aspect.ratio=1) +
#    expand_limits(y=0) +
#    labs(title = "Bytes In Use vs. accesses", 
#         y = "Bytes In Use", 
#         x = "accesses (logical access)",
#         colour = "" ) 

tmem_df = melt(tmemlist, id="accesses")
#rename(hrc_df,c("L1"="System"))

ggplot(data=tmem_df, aes(x=accesses, y=value, colour=L1) ) +
    geom_line() +
    theme_bw() +
    theme(legend.position="bottom", legend.box = "horizontal", aspect.ratio=1) +
    expand_limits(y=0) +
    labs(title = "memory use vs. accesses", 
         y = "memory use", 
         x = "accesses (logical access)",
         colour = "" ) 


etchr <- subset(df,app == 1 & subpolicy == "normal" & w_hit_rate != -1, select=c(live_items,w_hit_rate) )
psahr <- subset(df,app == 2 & subpolicy == "normal" & w_hit_rate != -1, select=c(live_items,w_hit_rate) )


hrlist <- list()
hrlist[["etc"]] = etchr
hrlist[["psa"]] = psahr

hrc_df = melt(hrlist, id="live_items")
#rename(hrc_df,c("L1"="System"))

ggplot(data=hrc_df, aes(x=live_items, y=value, colour=L1) ) +
    geom_line() +
    theme_bw() +
    theme(legend.position="bottom", legend.box = "horizontal", aspect.ratio=1) +
    expand_limits(y=0) +
    labs(title = "Hit Rate vs. items", 
         y = "Hit Rate", 
         x = "items",
         colour = "" ) 
