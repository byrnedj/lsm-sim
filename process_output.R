
library(reshape2)
library(ggplot2)
#library(svglite)
library(stringi)
library(plyr)
options(scipen=1)

df <- read.table("multi.normal.out",header=TRUE)
app1hr <- subset(df,app == 1 & subpolicy == "normal", select=c(time,hit_rate) )
app2hr <- subset(df,app == 2 & subpolicy == "normal", select=c(time,hit_rate) )

hrlist <- list()
hrlist[["app1"]] = app1hr
hrlist[["app2"]] = app2hr


app1bytes <- subset(df,app == 1 & subpolicy == "normal", select=c(time,bytes_in_use) )
app2bytes <- subset(df,app == 2 & subpolicy == "normal" , select=c(time,bytes_in_use) )

byteslist <- list()
byteslist[["app1"]] = app1bytes
byteslist[["app2"]] = app2bytes

app1shq <- subset(df,app == 1  & subpolicy == "normal", select=c(time,shadow_q_hits) )
app2shq <- subset(df,app == 2  & subpolicy == "normal", select=c(time,shadow_q_hits) )

shqlist <- list()
shqlist[["app1"]] = app1shq
shqlist[["app2"]] = app2shq

app1tmem <- subset(df,app == 1  & subpolicy == "normal", select=c(time,target_mem,credit_bytes) )
app2tmem <- subset(df,app == 2  & subpolicy == "normal", select=c(time,target_mem,credit_bytes) )

app1tmem$total = app1tmem$credit_bytes 
app2tmem$total = app2tmem$credit_bytes 

app1tmem <- subset(app1tmem, select=c(time,total) )
app2tmem <- subset(app2tmem, select=c(time,total) )

tmemlist <- list()
tmemlist[["app1"]] = app1tmem
tmemlist[["app2"]] = app2tmem

hrc_df = melt(hrlist, id="time")
#rename(hrc_df,c("L1"="System"))

ggplot(data=hrc_df, aes(x=time, y=value, colour=L1) ) +
    geom_line() +
    theme_bw() +
    theme(legend.position="bottom", legend.box = "horizontal", aspect.ratio=1) +
    expand_limits(y=0) +
    labs(title = "Hit Rate vs. Time", 
         y = "Hit Rate", 
         x = "Time (logical access)",
         colour = "" ) 

shq_df = melt(shqlist, id="time")
#rename(hrc_df,c("L1"="System"))

ggplot(data=shq_df, aes(x=time, y=value, colour=L1) ) +
    geom_line() +
    theme_bw() +
    theme(legend.position="bottom", legend.box = "horizontal", aspect.ratio=1) +
    expand_limits(y=0) +
    labs(title = "Shadow Queue Hits vs. Time", 
         y = "Shadow Queue Hits", 
         x = "Time (logical access)",
         colour = "" ) 

bytes_df = melt(byteslist, id="time")
#rename(hrc_df,c("L1"="System"))

ggplot(data=bytes_df, aes(x=time, y=value, colour=L1) ) +
    geom_line() +
    theme_bw() +
    theme(legend.position="bottom", legend.box = "horizontal", aspect.ratio=1) +
    expand_limits(y=0) +
    labs(title = "Bytes In Use vs. Time", 
         y = "Bytes In Use", 
         x = "Time (logical access)",
         colour = "" ) 

tmem_df = melt(tmemlist, id="time")
#rename(hrc_df,c("L1"="System"))

ggplot(data=tmem_df, aes(x=time, y=value, colour=L1) ) +
    geom_line() +
    theme_bw() +
    theme(legend.position="bottom", legend.box = "horizontal", aspect.ratio=1) +
    expand_limits(y=0) +
    labs(title = "Credit Bytes vs. Time", 
         y = "Credit Bytes", 
         x = "Time (logical access)",
         colour = "" ) 
