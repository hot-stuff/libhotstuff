#!/bin/bash

proj_server_bin="hotstuff-app"
proj_server_path="/home/ted/hot-stuff/$proj_server_bin"
proj_conf_name="hotstuff.conf"

peer_list="./nodes.txt"     # the list of nodes
conf_src="./hotstuff.gen.conf"
server_map="./server_map.txt"         # optional mapping from node ip to server ip
template_dir="template"     # the dir that keeps the content shared among all nodes
remote_base="/home/ted/testbed"  # remote dir used to keep files for the experiment
#remote_base="/tmp/"  # remote dir used to keep files for the experiment
remote_log="log"   # log filename
remote_user="ted"
copy_to_remote_pat="rsync -avz <local_path> <remote_user>@<remote_ip>:<remote_path>"
copy_from_remote_pat="rsync -avz <remote_user>@<remote_ip>:<remote_path> <local_path>"
exe_remote_pat="ssh <remote_user>@<remote_ip> bash"
run_remote_pat="cd \"<rworkdir>\"; gdb -ex r -ex bt -ex generate-core-file -ex q --args '$proj_server_path' --conf \"hotstuff.gen-sec<node_id>.conf\""
reset_remote_pat="pgrep -f '$proj_server_bin' | xargs kill -9"

fin_keyword="error:"  # the keyword indicating completion of execution
fin_chk_period=1
fin_chk_skip_pat='^([A-O][0-9]*)|(_ctl)$'
force_peer_list=0
async_num=128

function join { local IFS="$1"; shift; echo "$*"; }
function split {
    local IFS="$1"
    local arr=($2)
    echo "${arr[@]}"
}

function die { echo "$1"; exit 1; }

declare -A nodes
declare -A node_confs
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
        node_confs[${tup[0]}]="${tup0[@]:1}"
        echo "${tup[0]} => ${nodes[${tup[0]}]} & ${node_confs[${tup[0]}]}"
        let nodes_cnt++
    done
}

declare -A server_map
function get_server_map {
    {
        IFS=$'\n'
        map_list=($(cat "$1"))
    }
    IFS=$'\n \t'
    for pair in "${map_list[@]}"; do
        p=($pair)
        server_map[${p[0]}]="${p[1]}"
        echo "mapping ${p[0]} => ${p[1]}"
    done
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
    local rid="$4"
    local extra_conf=($5)
    local tmpldir="$workdir/$template_dir/"
    local node_tmpldir="$workdir/$rid"
    [[ $(execute_remote_cmd_stat "$node_ip" \
        "mkdir -p \"$rworkdir\"" \
        /dev/null) == 0 ]] || die "failed to create directory $rworkdir"
    copy_file "$copy_to_remote_pat" "$tmpldir" "$node_ip" "$rworkdir"
    for conf in "${extra_conf[@]}"; do
        copy_file "$copy_to_remote_pat" "$node_tmpldir/$conf" "$node_ip" "$rworkdir"
    done
}

function _remote_start {
    local workdir="$1"
    local rworkdir="$2"
    local node_id="$3"
    local node_ip="$4"
    local client_port="$5"
    local cmd="${run_remote_pat//<rworkdir>/$rworkdir}"
    cmd="${cmd//<node_id>/$node_id}"
    cmd="${cmd//<cport>/$client_port}"
    execute_remote_cmd_pid "$node_ip" "$cmd" \
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

function _remote_finished {
    _remote_exec "$1" "$2" "$3" "grep \"$fin_keyword\" \"$rworkdir/$remote_log\""
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
    cp "$server_map" "$workdir/server_map.txt"
    get_node_info "$workdir/peer_list.txt"
    get_server_map "$workdir/server_map.txt"
    echo "copying configuration file"
    rsync -avP "$conf_src" "$tmpldir/$proj_conf_name"
    echo "${node_list[@]}"
    cnt="${#nodes[@]}"
    #for rid in "${!nodes[@]}"; do
    rid=0
    c=0
    while [[ "$rid" -lt "$cnt" ]]; do
        local node_tmpldir="$workdir/$rid"
        local ip="$(get_ip_by_id $rid)"
        ip="${server_map[$ip]:-$ip}"
        local pport="$(get_peer_port_by_id $rid)"
        local cport="$(get_client_port_by_id $rid)"
        local rworkdir="$remote_base/$workdir/${rid}"
        local extra_conf_=(${node_confs[$rid]})
        rm -rf "$node_tmpldir"
        mkdir "$node_tmpldir"
        (
        local extra_conf=()
        for conf in "${extra_conf_[@]}"; do
            cp "$conf" "$node_tmpldir/"
            extra_conf+=($(basename "$conf"))
            copy_file "$copy_to_remote_pat" "$tmpldir/$conf" "$node_ip" "$rworkdir"
        done
        echo "Starting $rid @ $ip, $pport and $cport"
        _remote_load "$workdir" "$rworkdir" "$ip" "$rid" "${extra_conf[@]}"
        echo "$rid loaded"
        ) &
        let rid++
        let c++
        if [[ "$c" -eq "$async_num" ]]; then
            c=0
            wait
        fi
    done
    wait
    rid=0
    c=0
    #for rid in "${!nodes[@]}"; do
    while [[ "$rid" -lt "$cnt" ]]; do
        local ip="$(get_ip_by_id $rid)"
        ip="${server_map[$ip]:-$ip}"
        local pport="$(get_peer_port_by_id $rid)"
        local cport="$(get_client_port_by_id $rid)"
        local rworkdir="$remote_base/$workdir/${rid}"
        (
        echo "Starting $rid @ $ip, $pport and $cport"
        _remote_start "$workdir" "$rworkdir" "$rid" "$ip" "$cport"
        echo "$rid started"
        ) &
        let rid++
        let c++
        if [[ "$c" -eq "$async_num" ]]; then
            c=0
            wait
        fi
    done
    wait
}

function fetch_all {
    local workdir="$1"
    get_node_info "$workdir/peer_list.txt"
    get_server_map "$workdir/server_map.txt"
    for rid in "${!nodes[@]}"; do
        #if [[ "$rid" != 0 ]]; then
        #    continue
        #fi
        local ip="$(get_ip_by_id $rid)"
        ip="${server_map[$ip]:-$ip}"
        local port="$(get_peer_port_by_id $rid)"
        local rworkdir="$remote_base/$workdir/${rid}"
        local pid="$(cat $workdir/${rid}.pid)"
        local msg="Fetching $rid @ $ip, $port "
        _remote_fetch "$workdir" "$rworkdir" "$rid" "$ip" && echo "$msg: copied" || echo "$msg: failed" &
    done
    wait
}

function exec_all {
    local workdir="$1"
    local cmd="$2"
    get_node_info "$workdir/peer_list.txt"
    get_server_map "$workdir/server_map.txt"
    cnt="${#nodes[@]}"
    rid=0
    c=0
    #for rid in "${!nodes[@]}"; do
    while [[ "$rid" -lt "$cnt" ]]; do
        local ip="$(get_ip_by_id $rid)"
        ip="${server_map[$ip]:-$ip}"
        local port="$(get_peer_port_by_id $rid)"
        local rworkdir="$remote_base/$workdir/${rid}"
        local msg="Executing $rid @ $ip, $port "
        _remote_exec "$workdir" "$rworkdir" "$ip" "$cmd" && echo "$msg: succeeded" || echo "$msg: failed" &
        let rid++
        let c++
        if [[ "$c" -eq "$async_num" ]]; then
            c=0
            wait
        fi
    done
    wait
}

function reset_all {
    exec_all "$1" "$reset_remote_pat"
}

function stop_all {
    local workdir="$1"
    get_node_info "$workdir/peer_list.txt"
    get_server_map "$workdir/server_map.txt"
    for rid in "${!nodes[@]}"; do
        local ip="$(get_ip_by_id $rid)"
        ip="${server_map[$ip]:-$ip}"
        local port="$(get_peer_port_by_id $rid)"
        local rworkdir="$remote_base/$workdir/${rid}"
        local pid="$(cat $workdir/${rid}.pid)"
        local msg="Killing $rid @ $ip, $port "
        _remote_stop "$workdir" "$rworkdir" "$ip" "$pid" && echo "$msg: stopped" || echo "$msg: failed" &
    done
    wait
}

function status_all {
    local workdir="$1"
    get_node_info "$workdir/peer_list.txt"
    get_server_map "$workdir/server_map.txt"
    cnt="${#nodes[@]}"
    rid=0
    c=0
    #for rid in "${!nodes[@]}"; do
    while [[ "$rid" -lt "$cnt" ]]; do
        local ip="$(get_ip_by_id $rid)"
        ip="${server_map[$ip]:-$ip}"
        local port="$(get_peer_port_by_id $rid)"
        local rworkdir="$remote_base/$workdir/${rid}"
        local pid="$(cat $workdir/${rid}.pid)"
        local msg="$rid @ $ip, $port "
        _remote_status "$workdir" "$rworkdir" "$ip" "$pid" && echo "$msg: running" || echo "$msg: dead" &
        let rid++
        let c++
        if [[ "$c" -eq "$async_num" ]]; then
            c=0
            wait
        fi
    done
    wait
}

function finished_all {
    local workdir="$1"
    get_node_info "$workdir/peer_list.txt"
    get_server_map "$workdir/server_map.txt"
    for rid in "${!nodes[@]}"; do
        local ip="$(get_ip_by_id $rid)"
        ip="${server_map[$ip]:-$ip}"
        local port="$(get_peer_port_by_id $rid)"
        local rworkdir="$remote_base/$workdir/${rid}"
        if [[ "$rid" =~ $fin_chk_skip_pat ]]; then
            continue
        fi
        printf "$rid @ $ip, $port "
        _remote_finished "$workdir" "$rworkdir" "$ip" && echo "finished" || echo "in-progress"
    done
}

function wait_all {
    local workdir="$1"
    get_node_info "$workdir/peer_list.txt"
    get_server_map "$workdir/server_map.txt"
    while true; do
        finished=1
        printf "checking the nodes..."
        for rid in "${!nodes[@]}"; do
            local ip="$(get_ip_by_id $rid)"
            ip="${server_map[$ip]:-$ip}"
            local port="$(get_peer_port_by_id $rid)"
            local rworkdir="$remote_base/$workdir/${rid}"
            if [[ "$rid" =~ $fin_chk_skip_pat ]]; then
                continue
            fi
            if ! _remote_finished "$workdir" "$rworkdir" "$ip"; then
                finished=0
                break
            fi
        done
        if [[ $finished == 1 ]]; then
            break
        fi
        echo "not finished yet, wait for $fin_chk_period secs"
        sleep "$fin_chk_period"
    done
    echo "finished"
}

function check_all {
    status_all "$1" | grep dead -q
    [[ "$?" -eq 0 ]] && die "some nodes are dead"
    echo "ok"
}

function print_help {
echo "Usage: $0 [--bin] [--path] [--conf] [--conf-src] [--peer-list] [--server-map] [--user] [--force-peer-list] [--help] COMMAND WORKDIR

    --help                      show this help and exit
    --bin                       name of binary executable
    --path                      path to the binary
    --conf                      shared configuration filename
    --conf-src                  shared configuration source file
    --peer-list FILE            read peer list from FILE (default: $peer_list)
    --server-map FILE           read server map from FILE (default: $server_map)
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
server-map:,\
remote-base:,\
remote-user:,\
copy-to-remote-pat:,\
copy-from-remote-pat:,\
exe-remote-pat:,\
run-remote-pat:,\
reset-remote-pat:,\
fin-keyword:,\
fin-chk-period:,\
fin-chk-skip-pat:,\
force-peer-list,\
help'

PARSED=$(getopt --options "$SHORT" --longoptions "$LONG" --name "$0" -- "$@")
[[ $? -ne 0 ]] && exit 1
eval set -- "$PARSED"

while true; do
    case "$1" in
        --bin) proj_server_bin="$2"; shift 2;;
        --path) proj_server_path="$2"; shift 2;;
        --conf) proj_conf_name="$2"; shift 2;;
        --conf-src) conf_src="$2"; shift 2;;
        --peer-list) peer_list="$2"; shift 2;;
        --server-map) server_map="$2"; shift 2;;
        --remote-base) remote_base="$2"; shift 2;;
        --remote-user) remote_user="$2"; shift 2;;
        --copy-to-remote-pat) copy_to_remote_pat="$2"; shift 2;;
        --copy-from-remote-pat) copy_from_remote_pat="$2"; shift 2;;
        --exe-remote-pat) exe_remote_pat="$2"; shift 2;;
        --run-remote-pat) run_remote_pat="$2"; shift 2;;
        --reset-remote-pat) reset_remote_pat="$2"; shift 2;;
        --fin-keyword) fin_keyword="$2"; shift 2;;
        --fin-chk-period) fin_chk_period="$2"; shift 2;;
        --fin-chk-skip-pat) fin_chk_skip_pat="$2"; shift 2;;
        --force-peer-list) force_peer_list=1; shift 1;;
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
    finished) check_argnum 1 "$@" && finished_all "$1" ;;
    fetch) check_argnum 1 "$@" && fetch_all "$1" ;;
    wait) check_argnum 1 "$@" && wait_all "$1" ;;
    reset) check_argnum 1 "$@" && reset_all "$1" ;;
    exec) check_argnum 2 "$@" && exec_all "$1" "$2" ;;
    *) print_help ;;
esac
