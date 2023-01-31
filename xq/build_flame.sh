# !/bin/sh


if [ -z "$1" ]; then
	echo "please enter process'es name\n";
	exit;
fi

`sudo perf script -f -i ./perf/$1.data > ./perf/$1.unfold`
`../FlameGraph/stackcollapse-perf.pl ./perf/$1.unfold > ./perf/$1.folded`
`../FlameGraph/flamegraph.pl ./perf/$1.folded > ./perf/$1.svg`