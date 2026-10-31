"""Decode verified 2d357153fa50c442 skinning jobs, rejecting incomplete coverage."""
import struct

def decode_jobs(push,valid,header,job_list,records,groups):
    if len(push)<12 or len(valid)<12 or any(x!=1 for x in valid[:12]):raise ValueError('Missing push constants')
    if len(header)<20:raise ValueError('Missing physical batch header')
    batch=struct.unpack_from('<i',push,8)[0];stride=struct.unpack_from('<i',header,16)[0]
    if batch<0 or stride<0 or len(groups)!=3:raise ValueError('Unsupported dispatch dimensions/batch')
    if any(x==0 for x in groups):return []
    if groups[2]!=1:raise ValueError('Unsupported dispatch dimensions/batch')
    first=batch*stride;count=groups[1]
    if (first+count)*4>len(job_list):raise ValueError('Job-list prefix does not cover dispatch')
    jobs=[]
    for n in range(count):
        index=struct.unpack_from('<I',job_list,(first+n)*4)[0]
        if (index+1)*64>len(records):raise ValueError('Job-record prefix does not cover dispatch')
        words=struct.unpack_from('<16I',records,index*64)
        if words[0]>groups[0]*64:raise ValueError('Dispatch does not cover all job vertices')
        base=words[10];vertices=words[0]
        if base+vertices>0xffffffff//3:raise ValueError('Output index overflow')
        jobs.append(dict(job=index,vertexCount=vertices,sourceVertex=words[1],jointBase=words[9],outputOffset=base*12,outputBytes=vertices*12,secondaryOutput=words[11],flags=words[12],words=list(words)))
    return jobs

def indexed_offsets(indices,index_type,first,count,vertex_offset,selector):
    width={0:2,1:4}.get(index_type)
    if width is None or selector<2:raise ValueError('Unsupported index type/deformation selector')
    if (first+count)*width>len(indices):raise ValueError('Index payload incomplete')
    fmt='<H' if width==2 else '<I'
    return [12*(selector-2+((struct.unpack_from(fmt,indices,(first+i)*width)[0]+vertex_offset)&65535)) for i in range(count)]

def writers_for_offsets(jobs,offsets):
    result=[]
    for offset in offsets:
        matches=[j['job'] for j in jobs if j['outputOffset']<=offset and offset+12<=j['outputOffset']+j['outputBytes']]
        if len(matches)!=1:raise ValueError('Vertex has missing or ambiguous writer')
        result.append(matches[0])
    return sorted(set(result))
