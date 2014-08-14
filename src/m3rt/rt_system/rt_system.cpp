/*
M3 -- Meka Robotics Real-Time Control System
Copyright (c) 2010 Meka Robotics
Author: edsinger@mekabot.com (Aaron Edsinger)

M3 is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

M3 is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with M3.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "m3rt/rt_system/rt_system.h"
//#include "m3rt/base/m3ec_pdo_v1_def.h"
#include <unistd.h>
#include <string>

#if defined(__RTAI__) && defined(__cplusplus)
extern "C" {
#include <rtai.h>
#include <rtai_lxrt.h>
#include <rtai_sem.h>
#include <rtai_sched.h>
#include <rtai_nam2num.h>
#include <rtai_shm.h>
#include <rtai_malloc.h>	 
}
#endif



namespace m3rt
{
using namespace std;
static bool sys_thread_active;
static bool sys_thread_end;
static int step_cnt = 0;

unsigned long long getNanoSec(void)
{
    struct timeval tp;
    struct timezone tzp;

    tzp.tz_minuteswest = 0;

    (void) gettimeofday(&tp, &tzp); //
    return 1000000000LL * (long long) tp.tv_sec +
            1000LL * (long long) tp.tv_usec;
}

void *rt_system_thread(void *arg)
{
    M3RtSystem *m3sys = (M3RtSystem *)arg;
    sys_thread_end = true; //gonna be at true is startup fails=> no wait
	bool safeop_only = false;
    int tmp_cnt = 0;
	bool ready_sent=false;
    M3_INFO("Starting M3RtSystem real-time thread.\n");
#ifdef __RTAI__
	RTIME print_dt=1e9;
    RT_TASK *task=NULL;
    RTIME start, end, dt,tick_period;
			#ifdef ONESHOT_MODE
				M3_INFO("Oneshot mode activated.\n");
				rt_set_oneshot_mode();
			#endif
	if ( !(rt_is_hard_timer_running() ))
	{
		M3_INFO("Starting the real-time timer\n");
				#ifndef ONESHOT_MODE
				M3_INFO("Periodic mode activated.\n");
				rt_set_periodic_mode();
				#endif
		tick_period = start_rt_timer(nano2count(RT_TIMER_TICKS_NS) );
	}else{
		M3_INFO("Real-time timer running.\n");
		tick_period = nano2count(RT_TIMER_TICKS_NS);
	}
    M3_INFO("Beginning RTAI Initialization.\n");
	rt_allow_nonroot_hrt();
    ;
    if(!( task = rt_task_init_schmod(nam2num("M3SYS"), 0, 0, 0, SCHED_FIFO, 0xFF))) {
        m3rt::M3_ERR("Failed to create RT-TASK M3SYS\n", 0);
        sys_thread_active = false;
        return 0;
    }
    M3_INFO("RT Task Scheduled.\n");
    M3_INFO("Nonroot hrt initialized.\n");
    rt_task_use_fpu(task, 1);
    M3_INFO("Use fpu initialized.\n");
    mlockall(MCL_CURRENT | MCL_FUTURE);
    M3_INFO("Mem lock all initialized.\n");
	RTIME tick_period_orig = tick_period;
	if(!m3sys->IsHardRealTime()){
		M3_INFO("Soft real time initialized.\n");
		rt_make_soft_real_time();
	}else{
		M3_INFO("Hard real time initialized.\n");
		rt_make_hard_real_time();
	}
#endif

#if defined(__RTAI__)
	#ifndef ONESHOT_MODE
		rt_sleep(nano2count(5e8));
		RTIME now = rt_get_time();
		if(rt_task_make_periodic(task, now + tick_period, tick_period)) {
			M3_ERR("Couldn't make rt_system task periodic.\n");
			return 0;
		}
		M3_INFO("Periodic task initialized.\n");
		//rt_sleep(nano2count(1e9));
	#endif
#endif
		
#ifndef __RTAI__
    usleep(1e6);
    M3_INFO("Using pthreads\n");
#endif
	


#ifndef __RTAI__
    long long start, end, dt;
#endif

    m3sys->over_step_cnt = 0;
    
 #ifdef __RTAI__  
	RTIME print_start=rt_get_cpu_time_ns();
	RTIME diff=0;
	int dt_us,tick_period_us,overrun_us;
	//rt_sleep(nano2count((long long)1e8));
	
#endif
	sys_thread_end = false;
	sys_thread_active = true;
    while(!sys_thread_end) {
		tmp_cnt++;

#ifdef __RTAI__
		start = rt_get_cpu_time_ns();
#else
		start = getNanoSec();
#endif

        if(!m3sys->Step(safeop_only))  //This waits on m3ec.ko semaphore for timing
            break;
#ifdef __RTAI__
        end = rt_get_cpu_time_ns();
        dt = end - start;
        /*
        Check the time it takes to run components, and if it takes longer
        than our period, make us run slower. Otherwise this task locks
        up the CPU.*/
        if(dt > count2nano(tick_period) && step_cnt > 10) {
            m3sys->over_step_cnt++;
            int dt_us = static_cast<int>(((dt) / 1000));
            int tick_period_us = static_cast<int>(((count2nano(tick_period)) / 1000));
            int overrun_us = dt_us - tick_period_us;
            //rt_printk("Previous period: %d us overrun (dt: %d us, des_period: %d us)\n", overrun_us, dt_us, tick_period_us);
            if(m3sys->over_step_cnt > 10) {
                rt_printk("Step %d: Computation time of components is too long (dt:%d). Forcing all components to state SafeOp - switching to SAFE REALTIME.\n", step_cnt,(int)(dt/1000.0));
                rt_printk("Previous period: %d. New period: %d\n", (int)(count2nano(tick_period)/1000), (int)(dt/1000));
                tick_period = nano2count(dt);
				rt_make_soft_real_time();
				rt_set_period(task,tick_period);
                safeop_only = true;
                m3sys->over_step_cnt = 0;
            }else{
			/*if(tick_period>tick_period_orig)
			{
				tick_period--;
				rt_set_period(task,tick_period);
			}*/
			}
        } else {
            if(m3sys->over_step_cnt > 0){
                m3sys->over_step_cnt--;
			}
			/*if(tick_period>tick_period_orig)
			{
				tick_period--;
				rt_set_period(task,tick_period);
			}*/
        }
#ifndef ONESHOT_MODE
        rt_task_wait_period(); //No longer need as using sync semaphore of m3ec.ko // A.H : oneshot mode is too demanding => periodic mode is necessary !
		
#else
		diff = count2nano(tick_period)-(rt_get_cpu_time_ns()-start);
		rt_sleep(min((RTIME)0,nano2count(diff)));
#endif
	if (rt_get_time_ns() -print_start > print_dt)
        {
            rt_printk("M3RtSystem Freq : %d (dt: %d us)\n",tmp_cnt,(int)(dt/1000.0));
			tmp_cnt = 0;
			print_start = rt_get_time_ns();
			
        }
#else
		if(!ready_sent){
			sem_post(m3sys->ready_sem);
			ready_sent=true;
		}
        end = getNanoSec();
        dt = end - start;
        if(tmp_cnt++==1000)
        {
            tmp_cnt=0;
            std::cout<<"Loop computation time : "<<dt/1000<<" us (sleeping "<<( RT_TIMER_TICKS_NS - ((unsigned int)dt)) / 1000000 <<" us)"<<endl;
        }
        usleep((RT_TIMER_TICKS_NS - ((unsigned int)dt)) / 1000);
#endif
    }
#ifdef __RTAI__
    rt_make_soft_real_time();
    rt_task_delete(task);
#endif
    sys_thread_active = false;
    return 0;
}

M3RtSystem::~M3RtSystem() {}

////////////////////////////////////////////////////////////////////////////////////////////

bool M3RtSystem::Startup()
{
    sys_thread_active = false;
    sys_thread_end = true;
    BannerPrint(60, "Startup of M3RtSystem");
    if(!this->StartupComponents()) {
        sys_thread_active = false;
        return false;
    }
//#ifdef __RTAI__
///    hst = rt_thread_create((void *)rt_system_thread, (void *)this, 1000000);
	
	//rt_sem_wait_timed(this->ready_sem,nano2count(2e9)); A.H:doesn't work for some reason. FIXME: maybe because linux task (!rtai task)
//#else
	//struct timespec ts;
	//ts.tv_sec = 3;
    long ret = pthread_create((pthread_t *)&hst, NULL, (void * ( *)(void *))rt_system_thread, (void *)this);
	//sem_timedwait(ready_sem, &ts);
//#endif
    if(!(ret==0)){
		m3rt::M3_INFO("Startup of M3RtSystem thread failed (error code [%ld]).\n",ret);
        return false;
    }
    for(int i = 0; i < 20; i++) {
		if(sys_thread_active)
            break;
		M3_INFO("Waiting for the Ready Signal.\n");
        usleep(1e6); //Wait until enters hard real-time and components loaded. Can take some time if alot of components.max wait = 1sec
        
    }
    if(!sys_thread_active) {
        m3rt::M3_INFO("Startup of M3RtSystem thread failed, thread still not active.\n");
        return false;
    }
    //Debugging
    if(0) {
        /*M3_INFO("PDO Size M3ActPdoV1Cmd %d\n",(int)sizeof(M3ActPdoV1Cmd));
        M3_INFO("PDO Size M3ActPdoV1Status %d\n\n",(int)sizeof(M3ActPdoV1Status));

        M3_INFO("PDO Size M3ActX1PdoV1Cmd %d\n",(int)sizeof(M3ActX1PdoV1Cmd));
        M3_INFO("PDO Size M3ActX2PdoV1Cmd %d\n",(int)sizeof(M3ActX2PdoV1Cmd));
        M3_INFO("PDO Size M3ActX3PdoV1Cmd %d\n",(int)sizeof(M3ActX3PdoV1Cmd));
        M3_INFO("PDO Size M3ActX4PdoV1Cmd %d\n\n",(int)sizeof(M3ActX4PdoV1Cmd));

        M3_INFO("PDO Size M3ActX1PdoV1Status %d\n",(int)sizeof(M3ActX1PdoV1Status));
        M3_INFO("PDO Size M3ActX2PdoV1Status %d\n",(int)sizeof(M3ActX2PdoV1Status));
        M3_INFO("PDO Size M3ActX3PdoV1Status %d\n",(int)sizeof(M3ActX3PdoV1Status));
        M3_INFO("PDO Size M3ActX4PdoV1Status %d\n",(int)sizeof(M3ActX4PdoV1Status));*/

    }
    return true;
}

bool M3RtSystem::Shutdown()
{
    M3_INFO("Begin shutdown of M3RtSystem...\n");
    //Stop RtSystem thread
    sys_thread_end = true;
#ifdef __RTAI__
    //RTIME timeout = nano2count(rt_get_cpu_time_ns());
    int timeout_us = 2e6; //2s
    RTIME start_time = rt_get_time_ns();
    ///rt_thread_join(hst);
    while(sys_thread_active && (rt_get_time_ns()-start_time < timeout_us*1000))
    {
        M3_INFO("Waiting for Real-Time thread to shutdown...\n");
        usleep(500000);
    }
#else
    pthread_join((pthread_t)hst, NULL);
#endif
    if(sys_thread_active) {
        m3rt::M3_WARN("M3RtSystem thread did not shutdown correctly\n");
        //return false;
    }
#ifdef __RTAI__
    if(shm_ec != NULL)
#endif
    {
        //Send out final shutdown command to EC slaves
        for(int i = 0; i < GetNumComponents(); i++)
            GetComponent(i)->Shutdown();
#ifdef __RTAI__
        rt_shm_free(nam2num(SHMNAM_M3MKMD));
#endif
    }
    if(ext_sem != NULL) {
#ifdef __RTAI__
        rt_sem_delete(ext_sem);
#else
        delete ext_sem;
#endif
        ext_sem = NULL;
    }
        if(ready_sem != NULL) {
#ifdef __RTAI__
        rt_sem_delete(ready_sem);
#else
        delete ready_sem;
#endif
        ready_sem = NULL;
    }
    shm_ec = NULL;
    shm_sem = NULL;
    sync_sem = NULL;
    factory->ReleaseAllComponents();
    M3_INFO("Shutdown of M3RtSystem complete\n");
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////

bool M3RtSystem::StartupComponents()
{
	M3_INFO("Reading components config files ...\n");

    //if(!ReadConfigEc(M3_CONFIG_FILENAME))
    //    return false;
		//if(!ReadConfigRt(M3_CONFIG_FILENAME))
    //    return false;
	
	
	
	if(!ReadConfig(M3_CONFIG_FILENAME,"ec_components",this->m3ec_list,this->idx_map_ec))
		return false;
	if(!ReadConfig(M3_CONFIG_FILENAME,"rt_components",this->m3rt_list,this->idx_map_rt))
		return false;

    M3_INFO("Done reading components config files.\n");
#ifdef __RTAI__
    M3_INFO("Getting Kernel EC components.\n");
    shm_ec = (M3EcSystemShm *) rtai_malloc(nam2num(SHMNAM_M3MKMD), 1);
    if(shm_ec)
        M3_PRINTF("Found %d active M3 EtherCAT slaves\n", shm_ec->slaves_active);
    else {
        M3_ERR("Rtai_malloc failure for SHMNAM_M3KMOD\n", 0);
        return false;
    }
    shm_sem = (SEM *)rt_get_adr(nam2num(SEMNAM_M3LSHM));
    if(!shm_sem) {
        M3_ERR("Unable to find the SEMNAM_M3LSHM semaphore.\n", 0);
        return false;
    }

    sync_sem = (SEM *)rt_get_adr(nam2num(SEMNAM_M3SYNC));
    if(!sync_sem) {
        M3_ERR("Unable to find the SYNCSEM semaphore.\n", 0);
        return false;
    }
    ext_sem = rt_typed_sem_init(nam2num(SEMNAM_M3LEXT), 1, BIN_SEM);
#else
    ext_sem = new sem_t();
    sem_init(ext_sem, 1, 1);
#endif
    if(!ext_sem) {
        M3_ERR("Unable to find the M3LEXT semaphore (probably hasn't been cleared properly, reboot can solve this problem).\n");
        //return false;
    }

#ifdef __RTAI__
    ready_sem = rt_typed_sem_init(nam2num(SEMNAM_M3READY), 1, BIN_SEM);
#else
    ready_sem = new sem_t();
    sem_init(ready_sem, 1, 1);
#endif
    if(!ready_sem) {
        M3_ERR("Unable to find the M3READY semaphore.\n");
        //return false;
    }
    //Link dependent components. Drop failures.
    //Keep dropping until no failures
    vector<M3Component *> bad_link;
    bool failure = true;
    M3_INFO("Linking components ...\n");
    while(GetNumComponents() > 0 && failure) {
        bad_link.clear();
        failure = false;
        for(int i = 0; i < GetNumComponents(); i++) {
            if(!GetComponent(i)->LinkDependentComponents()) {
                M3_WARN("Failure LinkDependentComponents for %s\n", GetComponent(i)->GetName().c_str());
                failure = true;
                bad_link.push_back(GetComponent(i));
            }
        }
        if(failure) {
            vector<M3Component *>::iterator ci;
            vector<M3ComponentEc *>::iterator eci;
            for(int i = 0; i < bad_link.size(); i++) {
                for(eci = m3ec_list.begin(); eci != m3ec_list.end(); ++eci)
                    if((*eci) == bad_link[i]) {
                        //(*eci)->Shutdown();
                        m3ec_list.erase(eci);
                        break;
                    }
                for(ci = m3rt_list.begin(); ci != m3rt_list.end(); ++ci)
                    if((*ci) == bad_link[i]) {
                        //(*ci)->Shutdown();
                        m3rt_list.erase(ci);
                        break;
                    }
                factory->ReleaseComponent(bad_link[i]);
            }
        }
    }
    
    if(GetNumComponents() == 0) {
        M3_WARN("No M3 Components could be loaded....\n", 0);
        return false;
    }
    M3_INFO("Done linking components.\n");
    M3_INFO("Starting up components ...\n");
    for(int i = 0; i < GetNumComponents(); i++) {
        GetComponent(i)->Startup();
    }
    M3_INFO("Done starting up components.\n");
    CheckComponentStates();
    PrettyPrintComponentNames();
    //Setup Monitor
    M3MonitorStatus *s = factory->GetMonitorStatus();
    for(int i = 0; i < GetNumComponents(); i++) {
        M3MonitorComponent *c = s->add_components();
        c->set_name(GetComponent(i)->GetName());
    }
    for(int i = 0; i < NUM_EC_DOMAIN; i++) {
        s->add_ec_domains();
    }
    return true;
}

bool M3RtSystem::ParseCommandFromExt(M3CommandAll &msg)
{
    int idx, i;
    string s;

    for(i = 0; i < msg.name_cmd_size(); i++) {
        idx = GetComponentIdx(msg.name_cmd(i));
        if(idx >= 0) {
            s = msg.datum_cmd(i);
            GetComponent(idx)->ParseCommand(s);
        } else {
            //M3_WARN("Invalid Command component name %s in ParseCommandFromExt\n",s.c_str());
            M3_WARN("Invalid Command component name %s in ParseCommandFromExt\n", msg.name_cmd(i).c_str());

        }
    }

    for(i = 0; i < msg.name_param_size(); i++) {
        idx = GetComponentIdx(msg.name_param(i));
        if(idx >= 0) {
            s = msg.datum_param(i);
            GetComponent(idx)->ParseParam(s);
        } else {
            M3_WARN("Invalid Param component name %s in ParseCommandFromExt\n", s.c_str());
        }
    }

    return true;
}

bool M3RtSystem::SerializeStatusToExt(M3StatusAll &msg, vector<string>& names)
{
    for(int i = 0; i < names.size(); i++) {
        M3Component *m = GetComponent(names[i]);
        if(m != NULL) {
            string datum;
            if(!m->SerializeStatus(datum)) {
                //Bug where SerializeToString fails for bad message size
                //Patched protobuf/message.cc to allow but return false
                //FixME!
                //datum.clear();
            }
            if(i >= msg.datum_size()) {
                //Grow message
                msg.add_datum(datum);
                msg.add_name(names[i]);
            } else {
                msg.set_datum(i, datum);
                msg.set_name(i, names[i]);
            }
        }
    }
    return true;
}



void M3RtSystem::CheckComponentStates()
{
    for(int i = 0; i < GetNumComponents(); i++) {
        if(GetComponent(i)->IsStateError() && !safeop_required) { //All or none in OP
            M3_WARN("Component error detected for %s. Forcing to state SAFEOP\n", GetComponent(i)->GetName().c_str());

            safeop_required = true;
            GetComponent(i)->SetStateSafeOp();
            //return;
        }
    }
}

bool M3RtSystem::SetComponentStateOp(int idx)
{
    if(safeop_required)
        return false;
    if(idx < GetNumComponents() && idx >= 0)
        if(GetComponent(idx)->IsStateSafeOp()) {
            GetComponent(idx)->SetStateOp();
            return true;
        }
    return false;
}

bool M3RtSystem::SetComponentStateSafeOp(int idx)
{
    if(idx < GetNumComponents() && idx >= 0) {
        GetComponent(idx)->SetStateSafeOp();
        return true;
    }
    return false;
}
int M3RtSystem::GetComponentState(int idx)
{
    if(idx < GetNumComponents() && idx >= 0)
        return GetComponent(idx)->GetState();
    return -1;
}

void M3RtSystem::PrettyPrint()
{
    int nece = 0, nrte = 0, necs = 0, nrts = 0, neco = 0, nrto = 0;
    vector<M3ComponentEc *>::iterator i;
    for(i = m3ec_list.begin(); i != m3ec_list.end(); ++i) {
        if((*i)->IsStateError()) nece++;
        if((*i)->IsStateSafeOp()) necs++;
        if((*i)->IsStateOp()) neco++;
    }
    vector<M3Component *>::iterator j;
    for(j = m3rt_list.begin(); j != m3rt_list.end(); ++j) {
        if((*j)->IsStateError()) nrte++;
        if((*j)->IsStateSafeOp()) nrts++;
        if((*j)->IsStateOp()) nrto++;
    }

    BannerPrint(80, "M3 SYSTEM");
    M3_PRINTF("Operational: %s\n", IsOperational() ? "yes" : "no");
    M3_PRINTF("Ec components: %d\n", (int)m3ec_list.size());
    M3_PRINTF("Rt components: %d\n", (int)m3rt_list.size());
    M3_PRINTF("Ec components in error: %d\n", nece);
    M3_PRINTF("Rt components in error: %d\n", nrte);
    M3_PRINTF("Ec components in safeop: %d\n", necs);
    M3_PRINTF("Rt components in safeop: %d\n", nrts);
    M3_PRINTF("Ec components in op: %d\n", neco);
    M3_PRINTF("Rt components in op: %d\n", nrto);
}



void M3RtSystem::PrettyPrintComponentNames()
{
    BannerPrint(60, "M3 SYSTEM COMPONENTS");
    for(int i = 0; i < GetNumComponents(); i++)
        M3_PRINTF("%s\n", GetComponentName(i).c_str());
    BannerPrint(60, "");
}

void M3RtSystem::PrettyPrintComponents()
{
    PrettyPrint();
    for(int i = 0; i < GetNumComponents(); i++)
        GetComponent(i)->PrettyPrint();
    M3_PRINTF("\n\n\n");
}

void M3RtSystem::PrettyPrintComponent(int idx)
{
    if(idx < GetNumComponents() && idx >= 0)
        GetComponent(idx)->PrettyPrint();
}

bool M3RtSystem::Step(bool safeop_only)
{
#ifdef __RTAI__
    RTIME start, end, dt, start_c, end_c, start_p, end_p;
#else
    long long start, end, dt, start_c, end_c, start_p, end_p;
#endif
    vector<M3ComponentEc *>::iterator j;
    step_cnt++;

    /*
        1: Block External Data Service from chaging state
        2: Wait until EtherCAT mod signals finished a cycle (synchronize)
        3: Acquire lock on EtherCAT shared mem
        4: Get data from EtherCAT shared mem
        5: Step all components
        6: Transmit newly computed commands to EtherCAT shared mem
        7: Upload status to logger
        8: Release locks, allow External Data Service to update components if new data is available.
    */

    /*
        The order of components in m3ec_list and m3rt_list is important as they can overwrite eachother.
        Given components A, B, where A computes and sets value B.x .
        If we place A before B in the component list of m3_config.yml, then A.Step() will run before B.Step() within once cycle.
        Otherwise, B.Step() uses the A.x value from the previous cycle.

        If we have an External Data Service that sets B.x=e periodically, then A.Step() will overwrite value e.
        Therefore if we want to directly communicate with B.x from the outside world, we must not publish to component A.
    */
#ifdef __RTAI__
    rt_sem_wait(ext_sem);
    rt_sem_wait(sync_sem);
    rt_sem_wait(shm_sem);
    start = rt_get_cpu_time_ns();
#else
    sem_wait(ext_sem);
    start = getNanoSec();
#endif
    if(safeop_only) { // in case we are too slow
        for(int i = 0; i < GetNumComponents(); i++)
            if(GetComponent(i)->IsStateError()) {
                GetComponent(i)->SetStateSafeOp();
            }

    }

    //Do some bookkeeping
    M3MonitorStatus *s = factory->GetMonitorStatus();
    int nop = 0, nsop = 0, nerr = 0;
    for(int i = 0; i < GetNumComponents(); i++) {
        if(GetComponent(i)->IsStateError()) nerr++;
        if(GetComponent(i)->IsStateSafeOp()) nsop++;
        if(GetComponent(i)->IsStateOp()) nop++;
        M3MonitorComponent *c = s->mutable_components(i);
        c->set_state((M3COMP_STATE)GetComponent(i)->GetState());
    }
    if(m3ec_list.size() != 0) {
        for(int i = 0; i < NUM_EC_DOMAIN; i++) {
            s->mutable_ec_domains(i)->set_t_ecat_wait_rx(shm_ec->monitor[i].t_ecat_wait_rx);
            s->mutable_ec_domains(i)->set_t_ecat_rx(shm_ec->monitor[i].t_ecat_rx);
            s->mutable_ec_domains(i)->set_t_ecat_wait_shm(shm_ec->monitor[i].t_ecat_wait_shm);
            s->mutable_ec_domains(i)->set_t_ecat_shm(shm_ec->monitor[i].t_ecat_shm);
            s->mutable_ec_domains(i)->set_t_ecat_wait_tx(shm_ec->monitor[i].t_ecat_wait_tx);
            s->mutable_ec_domains(i)->set_t_ecat_tx(shm_ec->monitor[i].t_ecat_tx);
        }
    }
    s->set_num_components_safeop(nsop);
    s->set_num_components_op(nop);
    s->set_num_components_err(nerr);
    s->set_num_components(GetNumComponents());
    s->set_num_components_ec(m3ec_list.size());
    s->set_num_components_rt(m3rt_list.size());
    s->set_operational(IsOperational());
    s->set_num_ethercat_cycles(GetEcCounter());
#ifdef __RTAI__
    //Set timestamp for all
    int64_t ts = shm_ec->timestamp_ns / 1000;
#else
    int64_t ts = getNanoSec() / 1000;
#endif

    for(int i = 0; i < GetNumComponents(); i++)
        GetComponent(i)->SetTimestamp(ts);

#ifdef __RTAI__
    start_p = rt_get_cpu_time_ns();
#else
    start_p = getNanoSec();
#endif

#ifdef __RTAI__
    //Get Status from EtherCAT
    for(int j = 0; j <= MAX_PRIORITY; j++) {
        for(int i = 0; i < m3ec_list.size(); i++) { //=m3ec_list.begin(); j!=m3ec_list.end(); ++j)
            if(m3ec_list[i]->GetPriority() == j) {
                start_c = rt_get_cpu_time_ns();
                m3ec_list[i]->StepStatus();
                end_c = rt_get_cpu_time_ns();
                M3MonitorComponent *c = s->mutable_components(idx_map_ec[i]);
                c->set_cycle_time_status_us((mReal)(end_c - start_c) / 1000);
            }
        }
    }
#endif

    //Set Status on non-EC components
    for(int j = 0; j <= MAX_PRIORITY; j++) {
        for(int i = 0; i < m3rt_list.size(); i++) {
            if(m3rt_list[i]->GetPriority() == j) {

#ifdef __RTAI__
                start_c = rt_get_cpu_time_ns();
#else
                start_c = getNanoSec();
#endif
                m3rt_list[i]->StepStatus();
#ifdef __RTAI__
                end_c = rt_get_cpu_time_ns();
#else
                end_c = getNanoSec();
#endif
                M3MonitorComponent *c = s->mutable_components(idx_map_rt[i]);
                c->set_cycle_time_status_us((mReal)(end_c - start_c) / 1000);
            }
        }
    }
#ifdef __RTAI__
    end_p = rt_get_cpu_time_ns();
#else
    end_p = getNanoSec();
#endif
    s->set_cycle_time_status_us((mReal)(end_p - start_p) / 1000);
    //Set Command on non-EC components
    //Step components in reverse order
#ifdef __RTAI__
    start_p = rt_get_cpu_time_ns();
#else
    start_p = getNanoSec();
#endif

    for(int j = MAX_PRIORITY; j >= 0; j--) {
        for(int i = 0; i < m3rt_list.size(); i++) {
            if(m3rt_list[i]->GetPriority() == j) {

#ifdef __RTAI__
                start_c = rt_get_cpu_time_ns();
#else
                start_c = getNanoSec();
#endif
                m3rt_list[i]->StepCommand();
#ifdef __RTAI__
                end_c = rt_get_cpu_time_ns();
#else
                end_c = getNanoSec();
#endif
                M3MonitorComponent *c = s->mutable_components(idx_map_rt[i]);
                c->set_cycle_time_command_us((mReal)(end_c - start_c) / 1000);
            }
        }
    }
#ifdef __RTAI__
    //Send Command to EtherCAT
    for(int j = MAX_PRIORITY; j >= 0; j--) {
        for(int i = 0; i < m3ec_list.size(); i++) {
            if(m3ec_list[i]->GetPriority() == j) {
                start_c = rt_get_cpu_time_ns();
                m3ec_list[i]->StepCommand();
                end_c = rt_get_cpu_time_ns();
                M3MonitorComponent *c = s->mutable_components(idx_map_ec[i]);
                c->set_cycle_time_command_us((mReal)(end_c - start_c) / 1000);
            }

        }
    }
    end_p = rt_get_cpu_time_ns();
#else
    end_p = getNanoSec();
#endif
    s->set_cycle_time_command_us((mReal)(end_p - start_p) / 1000);
    //Now see if any errors raised
    CheckComponentStates();

    if(log_service) {
        logging = true;
        if(!log_service->Step())
            M3_DEBUG("Step() of log service failed.\n");
        logging = false;
    }
#ifdef __RTAI__
    end = rt_get_cpu_time_ns();
#else
    end = getNanoSec();
#endif
    mReal elapsed = (mReal)(end - start) / 1000;
    if(elapsed > s->cycle_time_max_us() && step_cnt > 10)
        s->set_cycle_time_max_us(elapsed);
    s->set_cycle_time_us(elapsed);
    int64_t period = end - last_cycle_time;
    mReal rate = 1 / (mReal)period;
    s->set_cycle_frequency_hz((mReal)(rate * 1000000000.0));
    last_cycle_time = end;
#ifdef __RTAI__
    rt_sem_signal(shm_sem);
    rt_sem_signal(ext_sem);
#else
    sem_post(ext_sem);
#endif
    return true;
}
/*
bool M3RtSystem::ReadConfigEc(const char *filename)
{
    YAML::Node doc;
    YAML::Emitter out;
    m3rt::GetYamlStream(filename, out);
#ifndef YAMLCPP_05
    
    std::stringstream stream(out.c_str());
    YAML::Parser parser(stream);
    while(parser.GetNextDocument(doc)) {
#else
    std::vector<YAML::Node> all_docs = YAML::LoadAll(out.c_str());
    for(std::vector<YAML::Node>::const_iterator it=all_docs.begin(); it!=all_docs.end();++it){
        doc = *it;
#endif
        //Make sure an ec_component section
#ifndef YAMLCPP_05
        if(!doc.FindValue("ec_components")) {
#else
        if(!doc["ec_components"]){
#endif
            M3_INFO("No ec_components key in m3_config.yml. Proceeding without it...\n");
            continue;
        }
        const YAML::Node& ec_components = doc["ec_components"];
#ifndef YAMLCPP_05
        for(YAML::Iterator it = ec_components.begin(); it != ec_components.end(); ++it) {
            string dir;
            it.first() >> dir;
#else
        for(YAML::const_iterator it = ec_components.begin(); it != ec_components.end(); ++it) {
            string dir;
            dir = it->first.as<std::string>() ;
#endif
            
#ifndef YAMLCPP_05
            for(YAML::Iterator it_dir = ec_components[dir.c_str()].begin();
                it_dir != ec_components[dir.c_str()].end(); ++it_dir) {
                string  name, type;
                it_dir.first() >> name;
                it_dir.second() >> type;
#else
            for(YAML::const_iterator it_dir = ec_components[dir.c_str()].begin();
                it_dir != ec_components[dir.c_str()].end(); ++it_dir) {
                string  name, type;
                name=it_dir->first.as<std::string>();
                type=it_dir->second.as<std::string>();
#endif
                
                

                M3ComponentEc *m = NULL;
                m = (M3ComponentEc *) factory->CreateComponent(type);
                if(m != NULL) {
                    m->SetFactory(factory);
                    string f = dir + "/" + name + ".yml";
                    try {
                        if(m->ReadConfig(f.c_str())) {
                            if(!m->SetSlaveEcShm(shm_ec->slave, shm_ec->slaves_responding))
                                factory->ReleaseComponent(m);
                            else {
                                m3ec_list.push_back(m);
                                idx_map_ec.push_back(GetNumComponents() - 1);
                            }
                        } else factory->ReleaseComponent(m);
                    } catch(YAML::TypedKeyNotFound<string> e) {
                        M3_WARN("Missing key: %s in config file for EC component %s \n", e.key.c_str(), name.c_str());
                        factory->ReleaseComponent(m);
                    } catch(YAML::RepresentationException e) {
                        M3_WARN("%s while parsing config files for EC component %s \n", e.what(), name.c_str());
                        factory->ReleaseComponent(m);
                    } catch(...) {
                        M3_WARN("Error while parsing config files for EC component %s \n", name.c_str());
                        factory->ReleaseComponent(m);
                    }
                }
            }
        }
    }
    return true;
}
*/


}










