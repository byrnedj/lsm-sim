source('common.R')
# Some things we need to add to simulator output:
# app id
# timestamp through log dump point


## to use this just source it from an R terminal session
## YOU MUST set the filename variable
d <- read.table(filename, header=TRUE)
d <- d[d$util != 0,]

u <- ggplot(d, aes(y=1-util, x=1:nrow(d))) +
  geom_point() +
  coord_cartesian(ylim=c(0.0000001,1.0)) +
  scale_y_log10() +
  myTheme

h <- ggplot(d, aes(y=1-d$hitrate, x=1:nrow(d))) +
  geom_point() +
  coord_cartesian(ylim=c(0.001,1.0)) +
  scale_y_log10() +
  myTheme

