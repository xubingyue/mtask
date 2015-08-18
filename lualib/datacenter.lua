local  mtask = require "mtask"

-- 注意：这三个 api 都会阻塞住当前 coroutine ，留心异步重入的问题。
--和 UniqueService 相比较，datacenter 使用起来更加灵活。
--你还可以通过它来交换 Multicast 的频道号等各种信息。
--但是，datacenter 其实是通过在 master 节点上部署了一个专门的数据中心服务来共享这些数据的。
--所有对 datacenter 的查询，都需要和中心节点通讯（如果你是多节点的架构的话），这有时会造成瓶颈。
--如果你只需要简单的服务地址管理，UniqueService 做的更好，它会在每个节点都缓存查询结果。


local datacenter = {}
-- 从 key1.key2 读一个值。
--这个 api 至少需要一个参数，如果传入多个参数，则用来读出树的一个分支
function datacenter.get(...)
	return mtask.call("DATACENTER","lua","QUERY",...)
end
--可以向 key1.key2 设置一个值 value 。
--这个 api 至少需要两个参数，没有特别限制树结构的层级数。
function datacenter.set(...)
	return mtask.call("DATACENTER","lua","UPDATE",...)
end
--同 get 方法，但如果读取的分支为 nil 时，这个函数会阻塞，直到有人更新这个分支才返回。
--当读写次序不确定，但你需要读到其它地方写入的数据后再做后续事情时，用它比循环尝试读取要好的多。
--wait 必须作用于一个叶节点，不能等待一个分支。
function datacenter.wait(...) 
	return mtask.call("DATACENTER","lua","WAIT",...)
end

return datacenter