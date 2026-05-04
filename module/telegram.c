#include <linux/fs.h>
#include <linux/un.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/net.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/uaccess.h>
#include <net/net_namespace.h>

#define MAX_DEVICES 1024

static int DEVICES_COUNT = 0;
static struct miscdevice *devices[MAX_DEVICES];

static ssize_t chat_write(struct file *file, const char __user *buf,
                          size_t count, loff_t *off) {
  char* client_buf;
  struct socket* sock = NULL;
  struct sockaddr_un addr = {0};
  struct msghdr msg = {0};
  struct kvec iov;
  size_t send_len = count;

  if (count == 0) return 0;

  client_buf = kmalloc(count + 1, GFP_KERNEL);
  if (!client_buf) return -ENOMEM;

  if (copy_from_user(client_buf, buf, count)) {
    kfree(client_buf);
    return -EFAULT;
  }
  client_buf[count] = '\0';

  if (send_len > 0 && client_buf[send_len - 1] == '\n') {
    client_buf[send_len - 1] = '\0';
    --send_len;
  }

  pr_info("chat_write called with string '%s'\n", client_buf);

  int ret = sock_create_kern(&init_net, AF_UNIX, SOCK_SEQPACKET, 0, &sock);
  if (ret < 0) {
    kfree(client_buf);
    return ret;
  }

  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, "/telegram.sock", sizeof(addr.sun_path) - 1);

  ret = kernel_connect(sock, (struct sockaddr_unsized*) &addr, sizeof(addr), 0);
  if (ret < 0) {
    sock_release(sock);
    kfree(client_buf);
    return ret;
  }

  iov.iov_base = "write";
  iov.iov_len = 5;
  ret = kernel_sendmsg(sock, &msg, &iov, 1, iov.iov_len);

  iov.iov_base = (char *)file->f_path.dentry->d_name.name;
  iov.iov_len = strlen(file->f_path.dentry->d_name.name);
  ret = kernel_sendmsg(sock, &msg, &iov, 1, iov.iov_len);

  iov.iov_base = client_buf;
  iov.iov_len = send_len;
  ret = kernel_sendmsg(sock, &msg, &iov, 1, iov.iov_len);

  sock_release(sock);
  kfree(client_buf);

  if (ret < 0) return ret;

  return count;
}

static ssize_t chat_read(struct file *file, char __user *buf, size_t count,
                         loff_t *off) {
  char *client_buf;
  struct socket *sock = NULL;
  struct sockaddr_un addr = {0};
  struct msghdr msg = {0};
  struct kvec iov;
  int ret;

  if (*off > 0) return 0;
  if (count == 0) return 0;

  client_buf = kmalloc(count + 1, GFP_KERNEL);
  if (!client_buf) return -ENOMEM;

  ret = sock_create_kern(&init_net, AF_UNIX, SOCK_SEQPACKET, 0, &sock);
  if (ret < 0) {
    kfree(client_buf);
    return ret;
  }

  addr.sun_family = AF_UNIX;
  strscpy(addr.sun_path, "/telegram.sock", sizeof(addr.sun_path));

  ret = kernel_connect(sock, (struct sockaddr_unsized*) &addr, sizeof(addr), 0);
  if (ret < 0) {
    sock_release(sock);
    kfree(client_buf);
    return ret;
  }

  iov.iov_base = "read";
  iov.iov_len = 4;
  ret = kernel_sendmsg(sock, &msg, &iov, 1, iov.iov_len);
  if (ret < 0) {
    sock_release(sock);
    kfree(client_buf);
    return ret;
  }

  iov.iov_base = (char *)file->f_path.dentry->d_name.name;
  iov.iov_len = strlen(file->f_path.dentry->d_name.name);
  ret = kernel_sendmsg(sock, &msg, &iov, 1, iov.iov_len);
  if (ret < 0) {
    sock_release(sock);
    kfree(client_buf);
    return ret;
  }

  iov.iov_base = client_buf;
  iov.iov_len = count;
  ret = kernel_recvmsg(sock, &msg, &iov, 1, count, 0);
  if (ret < 0) {
    sock_release(sock);
    kfree(client_buf);
    return ret;
  }

  if (copy_to_user(buf, client_buf, ret)) {
    sock_release(sock);
    kfree(client_buf);
    return -EFAULT;
  }

  sock_release(sock);
  kfree(client_buf);
  *off += ret;
  return ret;
}

static const struct file_operations chat_fops = {
    .owner = THIS_MODULE,
    .write = chat_write,
    .read = chat_read,
};

static ssize_t create_chat(struct file *file, const char __user *buf,
                           size_t count, loff_t *off) {
  if (count == 0) return -EINVAL;
  if (DEVICES_COUNT >= MAX_DEVICES) return -ENOSPC;

  char *chat_name = kmalloc(count + 1, GFP_KERNEL);
  if (!chat_name) return -ENOMEM;

  if (copy_from_user(chat_name, buf, count)) {
    kfree(chat_name);
    return -EFAULT;
  }
  chat_name[count] = '\0';

  if (count > 0 && chat_name[count - 1] == '\n') {
    chat_name[count - 1] = '\0';
  }

  if (strlen(chat_name) == 0) {
    kfree(chat_name);
    return -EINVAL;
  }

  if (strchr(chat_name, '/')) {
    kfree(chat_name);
    return -EINVAL;
  }

  pr_info("create_chat: Creating chat with name '%s'\n", chat_name);

  struct miscdevice *chat_miscdev = kmalloc(sizeof(*chat_miscdev), GFP_KERNEL);
  if (!chat_miscdev) {
    kfree(chat_name);
    return -ENOMEM;
  }

  chat_miscdev->name = kasprintf(GFP_KERNEL, "telegram/%s", chat_name);
  if (!chat_miscdev->name) {
    kfree(chat_miscdev);
    kfree(chat_name);
    return -ENOMEM;
  }

  chat_miscdev->minor = MISC_DYNAMIC_MINOR;
  chat_miscdev->fops = &chat_fops;
  chat_miscdev->mode = 0666;

  int ret = misc_register(chat_miscdev);
  if (ret) {
    kfree(chat_miscdev->name);
    kfree(chat_miscdev);
    kfree(chat_name);
    return ret;
  }

  devices[DEVICES_COUNT++] = chat_miscdev;

  kfree(chat_name);
  return count;
}

static const struct file_operations create_chat_fops = {
    .owner = THIS_MODULE,
    .write = create_chat,
};

static struct miscdevice miscdev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "telegram/create_chat",
    .fops = &create_chat_fops,
    .mode = 0666,
};

static int __init create_chat_init(void) {
  int ret = misc_register(&miscdev);
  if (ret) return ret;
  return 0;
}

static void __exit create_chat_exit(void) {
  for (int i = 0; i < DEVICES_COUNT; i++) {
    pr_info("Removing chat device '%s'\n", devices[i]->name);
    misc_deregister(devices[i]);
    kfree(devices[i]->name);
    kfree(devices[i]);
  }
  DEVICES_COUNT = 0;

  misc_deregister(&miscdev);
}

module_init(create_chat_init);
module_exit(create_chat_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Linux Kernel Course");
MODULE_DESCRIPTION("Educational ioctl-based kernel interface");
