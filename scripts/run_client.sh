#!/bin/bash

proj_client_bin="hotstuff-client"
proj_client_path="/home/ted/hot-stuff/$proj_client_bin"
proj_conf_name="hotstuff.conf"

peer_list="./nodes.txt"     # the list of nodes
client_list="./clients.txt"  # the list of clients
conf_src="./hotstuff.gen.conf"
template_dir="template"     # the dir that keeps the content shared among all nodes
remote_base="/home/ted/testbed"  # remote dir used to keep files for the experiment
#remote_base="/tmp/"  # remote dir used to keep files for the experiment
remote_log="log"   # log filename
remote_user="ted"
copy_to_remote_pat="rsync -avz <local_path> <remote_user>@<remote_ip>:<remote_path>"
copy_from_remote_pat="rsync -avz <remote_user>@<remote_ip>:<remote_path> <local_path>"
exe_remote_pat="ssh <remote_user>@<remote_ip> bash"
run_remote_pat="cd \"<rworkdir>\"; '$proj_client_path' --idx \"<node_id>\" --iter -1 --max-async 3"
reset_remote_pat="pgrep -f '$proj_client_bin' | xargs kill -9"
node_id_step=1

function join { local IFS="$1"; shift; echo "$*"; }
function split {
    local IFS="$1"
    local arr=($2)
    echo "${arr[@]}"
}

function die { echo "$1"; exit 1; }

declare -A nodes
nodes_cnt=0
function get_node_info {
    pl="$1"
    if [[ "$force_peer_list" == 1 ]]; then
        pl="$peer_list"
    fi
    OIFS="$IFS"
    IFS=$'\n'
    node_list=($(cat "$pl"))
    IFS="$OIFS"
    for tuple in "${node_list[@]}"; do
        tup0=($(split $'\t' "$tuple"))
        tup=($(split : "${tup0[0]}"))
        nodes[${tup[0]}]="${tup[1]}:${tup[2]}"
        echo "${tup[0]} => ${nodes[${tup[0]}]}"
        let nodes_cnt++
    done
}

function get_client_info {
    cip_list=($(cat "$1"))
}


function get_addr {
    tup=($(split ';' $1))
    echo "${tup[0]}"
}

function get_ip {
    tup=($(split : $1))
    echo "${tup[0]}"
}

function get_peer_port {
    tup=($(split : $1))
    tup2=($(split ';' ${tup[1]}))
    echo "${tup2[0]}"
}


function get_client_port {
    tup=($(split : $1))
    tup2=($(split ';' ${tup[1]}))
    echo "${tup2[1]}"
}


function get_ip_by_id {
    get_ip "${nodes[$1]}"
}

function get_peer_port_by_id {
    get_peer_port "${nodes[$1]}"
}


function get_client_port_by_id {
    get_client_port "${nodes[$1]}"
}

function copy_file {
    local pat="$1"
    local cmd="${pat//<local_path>/$2}"
    cmd="${cmd//<remote_ip>/$3}"
    cmd="${cmd//<remote_user>/$remote_user}"
    cmd="${cmd//<remote_path>/$4}"
    echo $cmd
    eval "$cmd"
} >> log 2>&1

function execute_remote_cmd_pid {
    local node_ip="$1"
    local c="$2"
    local l="$3"
    local cmd="${exe_remote_pat//<remote_ip>/$node_ip}"
    cmd="${cmd//<remote_user>/$remote_user}"
    eval $cmd << EOF
$c > $l 2>&1 & echo \$!
EOF
}



function execute_remote_cmd_stat {
    local node_ip="$1"
    local c="$2"
    local l="$3"
    local cmd="${exe_remote_pat//<remote_ip>/$node_ip}"
    cmd="${cmd//<remote_user>/$remote_user}"
    eval $cmd << EOF
$c > $l 2>&1 ; echo \$?
EOF
}


function _remote_load {
    local workdir="$1"
    local rworkdir="$2"
    local node_ip="$3"
    local tmpldir="$workdir/$template_dir/"
    [[ $(execute_remote_cmd_stat "$node_ip" \
        "mkdir -p \"$rworkdir\"" \
        /dev/null) == 0 ]] || die "failed to create directory $rworkdir"
    copy_file "$copy_to_remote_pat" "$tmpldir" "$node_ip" "$rworkdir"
}

function _remote_start {
    local workdir="$1"
    local rworkdir="$2"
    local node_id="$3"
    local node_ip="$4"
    local client_port="$5"
    local client_ip="$6"
    local cmd="${run_remote_pat//<rworkdir>/$rworkdir}"
    cmd="${cmd//<node_id_step>/$node_id_step}"
    cmd="${cmd//<node_id>/$((node_id * node_id_step))}"
    cmd="${cmd//<server>/$node_ip:$client_port}"
    execute_remote_cmd_pid "$client_ip" "$cmd" \
        "\"$rworkdir/$remote_log\"" > "$workdir/${node_id}.pid"
}

function _remote_exec {
    local workdir="$1"
    local rworkdir="$2"
    local node_ip="$3"
    local cmd="$4"
    [[ $(execute_remote_cmd_stat "$node_ip" "$cmd" /dev/null) == 0 ]]
}

function _remote_stop {
    local node_pid="$4"
    _remote_exec "$1" "$2" "$3" "kill $node_pid"
}

function _remote_status {
    local node_pid="$4"
    _remote_exec "$1" "$2" "$3" "kill -0 $node_pid"
}

function _remote_fetch {
    local workdir="$1"
    local rworkdir="$2"
    local node_id="$3"
    local node_ip="$4"
    copy_file "$copy_from_remote_pat" "$workdir/${node_id}.log" "$node_ip" "$rworkdir/$remote_log"
}

function start_all {
    local workdir="$1"
    local tmpldir="$workdir/$template_dir/"
    mkdir "$workdir" > /dev/null 2>&1 || die "workdir already exists"
    rm -rf "$tmpldir"
    mkdir "$tmpldir"
    cp "$peer_list" "$workdir/peer_list.txt"
    cp "$client_list" "$workdir/client_list.txt"
    get_node_info "$workdir/peer_list.txt"
    get_client_info "$workdir/client_list.txt"
    echo "coyping configuration file"
    rsync -avP "$conf_src" "$tmpldir/$proj_conf_name"
    local i=0
    local j=0
    for cip in "${cip_list[@]}"; do
        local rid="${nodes[$i]}"
        local ip="$(get_ip_by_id $rid)"
        local pport="$(get_peer_port_by_id $rid)"
        local cport="$(get_client_port_by_id $rid)"
        local rworkdir="$remote_base/$workdir/${j}"
        (
        echo "Starting a client @ $cip, connecting to server #$rid @ $ip:$cport"
        _remote_load "$workdir" "$rworkdir" "$cip"
        _remote_start "$workdir" "$rworkdir" "$j" "$ip" "$cport" "$cip"
        echo "client #$j started"
        ) &
        let i++
        let j++
        if [[ "$i" -eq "${#nodes[@]}" ]]; then
            i=0
        fi
    done
    wait
}

function fetch_all {
    local workdir="$1"
    get_client_info "$workdir/client_list.txt"
    local i=0
    for cip in "${cip_list[@]}"; do
        local rworkdir="$remote_base/$workdir/${i}"
        local pid="$(cat $workdir/${i}.pid)"
        local msg="Fetching $i @ $cip"
        _remote_fetch "$workdir" "$rworkdir" "$i" "$cip" && echo "$msg: copied" || echo "$msg: failed" &
        let i++
    done
    wait
}

function exec_all {
    local workdir="$1"
    local cmd="$2"
    get_client_info "$workdir/client_list.txt"
    local i=0
    for cip in "${cip_list[@]}"; do
        local rworkdir="$remote_base/$workdir/${i}"
        local msg="Executing $i @ $cip"
        _remote_exec "$workdir" "$rworkdir" "$cip" "$cmd" && echo "$msg: succeeded" || echo "$msg: failed" &
        let i++
    done
    wait
}

function reset_all {
    exec_all "$1" "$reset_remote_pat"
}

function stop_all {
    local workdir="$1"
    get_client_info "$workdir/client_list.txt"
    local i=0
    for cip in "${cip_list[@]}"; do
        local rworkdir="$remote_base/$workdir/${i}"
        local pid="$(cat $workdir/${i}.pid)"
        local msg="Killing $i @ $cip"
        _remote_stop "$workdir" "$rworkdir" "$cip" "$pid" && echo "$msg: stopped" || echo "$msg: failed" &
        let i++
    done
    wait
}

function status_all {
    local workdir="$1"
    get_client_info "$workdir/client_list.txt"
    local i=0
    for cip in "${cip_list[@]}"; do
        local rworkdir="$remote_base/$workdir/${i}"
        local pid="$(cat $workdir/${i}.pid)"
        local msg="$i @ $cip"
        _remote_status "$workdir" "$rworkdir" "$cip" "$pid" && echo "$msg: running" || echo "$msg: dead" &
        let i++
    done
    wait
}

function check_all {
    status_all "$1" | grep dead -q
    [[ "$?" -eq 0 ]] && die "some nodes are dead"
    echo "ok"
}

function print_help {
echo "Usage: $0 [--bin] [--path] [--conf] [--conf-src] [--peer-list] [--client-list] [--user] [--force-peer-list] [--help] COMMAND WORKDIR

    --help                      show this help and exit
    --bin                       name of binary executable
    --path                      path to the binary
    --conf                      shared configuration filename
    --conf-src                  shared configuration source file
    --peer-list FILE            read peer list from FILE (default: $peer_list)
    --client-list FILE          read client list from FILE (default: $client_list)
    --user      USER            the username to login the remote machines
    --force-peer-list           force the use of FILE specified by --peer-list
                                instead of the peer list in WORKDIR"
    exit 0
}

function check_argnum {
    argnum=$(($# - 1))
    [[ "$1" -eq "$argnum" ]] || die "incorrect argnum: got $argnum, $1 expected"
}

getopt --test > /dev/null
[[ $? -ne 4 ]] && die "getopt unsupported"

SHORT=
LONG='\
bin:,path:,conf:,conf-src:,\
peer-list:,\
client-list:,\
remote-base:,\
remote-user:,\
copy-to-remote-pat:,\
copy-from-remote-pat:,\
exe-remote-pat:,\
run-remote-pat:,\
reset-remote-pat:,\
force-peer-list,\
node-id-step:,\
help'

PARSED=$(getopt --options "$SHORT" --longoptions "$LONG" --name "$0" -- "$@")
[[ $? -ne 0 ]] && exit 1
eval set -- "$PARSED"

while true; do
    case "$1" in
        --bin) proj_client_bin="$2"; shift 2;;
        --path) proj_client_path="$2"; shift 2;;
        --conf) proj_conf_name="$2"; shift 2;;
        --conf-src) conf_src="$2"; shift 2;;
        --peer-list) peer_list="$2"; shift 2;;
        --client-list) client_list="$2"; shift 2;;
        --remote-base) remote_base="$2"; shift 2;;
        --remote-user) remote_user="$2"; shift 2;;
        --copy-to-remote-pat) copy_to_remote_pat="$2"; shift 2;;
        --copy-from-remote-pat) copy_from_remote_pat="$2"; shift 2;;
        --exe-remote-pat) exe_remote_pat="$2"; shift 2;;
        --run-remote-pat) run_remote_pat="$2"; shift 2;;
        --reset-remote-pat) reset_remote_pat="$2"; shift 2;;
        --node-id-step) node_id_step="$2"; shift 2;;
        --help) print_help; shift 1;;
        --) shift; break;;
        *) die "internal error";;
    esac
done
cmd="$1"
shift 1
case "$cmd" in
    start) check_argnum 1 "$@" && start_all "$1" ;;
    stop) check_argnum 1 "$@" && stop_all "$1" ;;
    status) check_argnum 1 "$@" && status_all "$1" ;;
    check) check_argnum 1 "$@" && check_all "$1" ;;
    fetch) check_argnum 1 "$@" && fetch_all "$1" ;;
    reset) check_argnum 1 "$@" && reset_all "$1" ;;
    exec) check_argnum 2 "$@" && exec_all "$1" "$2" ;;
    *) print_help ;;
esac
